// SPDX-License-Identifier: EUPL-1.2
// Copyright (c) 2026 Chura IT Consultancy (Kustgloed)

#include "render_engine.h"

#include <string.h>

namespace KiteWindRender {

namespace {

// Floor division for possibly negative numerators (denominator > 0), independent of the
// implementation-defined rounding of signed division and shifts.
int64_t floorDiv(int64_t num, int64_t den) {
  int64_t q = num / den;
  if ((num % den != 0) && (num < 0)) q--;
  return q;
}

// Q30 fixed point (1.0 == 1 << 30). All intermediates stay below 2^63.
const int FRAC = 30;
const int64_t ONE = static_cast<int64_t>(1) << FRAC;
const int64_t HALF_PI = 1686629713;  // round(pi/2 * 2^30)

// sin(a) for a in [0, pi/2] (Q30) by a 15th-order Taylor series in Horner form; truncation
// error below 1e-11, so rounding is dominated by the ~1e-9 Q30 resolution.
int64_t sinQ30(int64_t a) {
  const int64_t a2 = (a * a) >> FRAC;
  static const int32_t divisor[] = {6, 20, 42, 72, 110, 156, 210};  // (2k)(2k+1)
  int64_t q = ONE;
  for (int k = 6; k >= 0; k--) q = ONE - (((a2 * q) >> FRAC) / divisor[k]);
  return (a * q) >> FRAC;
}

}  // namespace

// Matches Java RenderContract.linear: from + floor((2*delta*c + d) / (2*d)), t clamped.
int32_t linearValue(int32_t from, int32_t to, int32_t durationMs, int32_t t) {
  int64_t c = t < 0 ? 0 : (t > durationMs ? durationMs : t);
  int64_t delta = static_cast<int64_t>(to) - from;
  return static_cast<int32_t>(from + floorDiv(2 * delta * c + durationMs, 2LL * durationMs));
}

// Java: from + (to-from) * (1-cos(pi*t/d))/2, rounded half up, exact endpoints.
// Here (1-cos(x))/2 == sin^2(x/2) is evaluated in Q30 integer arithmetic: u = t/d, a = u*pi/2,
// w = sin(a)^2, result = from + floor((delta*w + 0.5) in Q30). Tolerance versus the Java double
// result: |w error| below about 2e-9, so values differ only when the exact value lies within
// 2e-9 * |delta| of a rounding tie (for the 0..100 percent range, within 2e-7), and then by 1.
int32_t cosineValue(int32_t from, int32_t to, int32_t durationMs, int32_t t) {
  if (t <= 0) return from;
  if (t >= durationMs) return to;
  const int64_t u = (static_cast<int64_t>(t) << FRAC) / durationMs;  // [0, 1) in Q30
  const int64_t a = (u * HALF_PI) >> FRAC;
  const int64_t s = sinQ30(a);
  const int64_t w = (s * s) >> FRAC;
  const int64_t delta = static_cast<int64_t>(to) - from;
  return static_cast<int32_t>(from + floorDiv(delta * w + (ONE >> 1), ONE));
}

// Segment [start, end) ownership as in Java Track.valueAt.
int32_t trackValueAt(const Definition& def, const Track& track, int64_t t) {
  const Segment* seg = &def.segments[track.firstSegment];
  if (track.kind == Kind::Constant || t <= 0) return seg[0].from;
  int64_t start = 0;
  for (uint16_t i = 0; i < track.segmentCount; i++) {
    if (t < start + seg[i].durationMs) {
      const int32_t local = static_cast<int32_t>(t - start);
      return track.kind == Kind::Linear
                 ? linearValue(seg[i].from, seg[i].to, seg[i].durationMs, local)
                 : cosineValue(seg[i].from, seg[i].to, seg[i].durationMs, local);
    }
    start += seg[i].durationMs;
  }
  return seg[track.segmentCount - 1].to;
}

Engine::Engine()
    : stagedMeta_(),
      activeDef_(0),
      activeIn_(0),
      haveDef_(false),
      haveInputs_(false),
      generation_(0),
      ceiling_(0),  // fail safe until the firmware sets its enforced limit
      state_(IDLE),
      lastMs_(0),
      elapsedMs_(0) {
  memset(defs_, 0, sizeof(defs_));
  memset(ins_, 0, sizeof(ins_));
  activeDigest_[0] = '\0';
}

Error Engine::stage(const char* body, size_t len, const char* expectedModel) {
  int s = state_.load(std::memory_order_acquire);
  for (;;) {
    if (s == STAGING || s == ACTIVATING) return Error::Busy;
    if (state_.compare_exchange_weak(s, STAGING, std::memory_order_acq_rel)) break;
  }
  // Exclusive owner of the inactive buffers; activation cannot run while STAGING.
  Error e = parseEnvelope(body, len, expectedModel, stagedMeta_, defs_[1 - activeDef_],
                          ins_[1 - activeIn_]);
  state_.store(e == Error::Ok ? READY : IDLE, std::memory_order_release);
  return e;
}

Activation Engine::activate(Error* reason) {
  int expected = READY;
  if (!state_.compare_exchange_strong(expected, ACTIVATING, std::memory_order_acq_rel))
    return Activation::Nothing;
  Error err = Error::Ok;
  const bool hasDef = stagedMeta_.hasDefinition;
  const bool hasIn = stagedMeta_.hasInputs;
  if (!hasDef && !haveDef_) {
    err = Error::NoDefinition;
  } else if (!hasDef && strcmp(stagedMeta_.digest, activeDigest_) != 0) {
    err = Error::BadDigest;  // server omitted a definition we do not hold
  } else {
    const Definition& d = hasDef ? defs_[1 - activeDef_] : defs_[activeDef_];
    if (hasIn) {
      err = validateInputs(d, ins_[1 - activeIn_]);
    } else if (haveInputs_) {
      err = validateInputs(d, ins_[activeIn_]);  // old inputs must fit the new definition
    }
  }
  if (err != Error::Ok) {
    state_.store(IDLE, std::memory_order_release);
    if (reason) *reason = err;
    return Activation::Rejected;
  }
  // Commit: index flips only, nothing is copied or half applied.
  if (hasDef) {
    activeDef_ = 1 - activeDef_;
    haveDef_ = true;
    memcpy(activeDigest_, stagedMeta_.digest, sizeof(activeDigest_));
  }
  if (hasIn) {
    activeIn_ = 1 - activeIn_;
    haveInputs_ = true;
    generation_ = ins_[activeIn_].generation;
  }
  state_.store(IDLE, std::memory_order_release);
  return Activation::Activated;
}

uint8_t Engine::effectiveBrightness() const {
  if (!haveDef_) return 0;
  const uint16_t requested = defs_[activeDef_].brightness;
  return static_cast<uint8_t>(requested < ceiling_ ? requested : ceiling_);
}

void Engine::render(uint32_t nowMs, Rgb* out, size_t count) {
  elapsedMs_ += static_cast<uint32_t>(nowMs - lastMs_);  // wrap-safe
  lastMs_ = nowMs;
  for (size_t i = 0; i < count; i++) out[i].r = out[i].g = out[i].b = 0;
  if (!haveDef_ || !haveInputs_) return;

  const Definition& d = defs_[activeDef_];
  const Inputs& in = ins_[activeIn_];
  const uint32_t brightness = effectiveBrightness();
  if (brightness == 0) return;
  // Shared clock 0; each track loops over its own length so differing periods stay aligned.
  const uint64_t clockMs = elapsedMs_ + (d.clockCount ? static_cast<uint32_t>(d.clocks[0].offsetMs) : 0u);

  for (uint16_t i = 0; i < in.bindingCount; i++) {
    const Binding& b = in.bindings[i];
    if (b.pixel >= count) continue;
    const Track& track = d.tracks[b.trackRef];
    int64_t total = 0;
    for (uint16_t k = 0; k < track.segmentCount; k++) total += d.segments[track.firstSegment + k].durationMs;
    const int64_t t = total > 0 ? static_cast<int64_t>(clockMs % static_cast<uint64_t>(total)) : 0;
    int32_t level = trackValueAt(d, track, t);  // percent of the effective brightness
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    const uint32_t rgb = d.palette[b.paletteRef];
    const uint32_t scale = brightness * static_cast<uint32_t>(level);  // <= 255 * 100
    // Round half up: channel * scale / (255 * 100).
    out[b.pixel].r = static_cast<uint8_t>((((rgb >> 16) & 0xFF) * scale + 12750) / 25500);
    out[b.pixel].g = static_cast<uint8_t>((((rgb >> 8) & 0xFF) * scale + 12750) / 25500);
    out[b.pixel].b = static_cast<uint8_t>(((rgb & 0xFF) * scale + 12750) / 25500);
  }
}

}  // namespace KiteWindRender
