// SPDX-License-Identifier: EUPL-1.2
// Copyright (c) 2026 Chura IT Consultancy (Kustgloed)

#ifndef KITE_WIND_RENDER_ENGINE_H
#define KITE_WIND_RENDER_ENGINE_H

// Generic player for the version-1 render contract. Knows tracks, gates, clocks, palette, and
// bindings only; no animation, profile, or weather names. No allocation after construction.
//
// Threading: stage() runs on the network task only; activate(), render(), hasActive() and
// effectiveBrightness() must all run on the same (render) thread. Staging writes only the inactive buffers, activation flips buffer indexes in the render
// loop, so a frame never sees a half-built definition.

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include "render_contract.h"

namespace KiteWindRender {

struct Rgb {
  uint8_t r, g, b;
};

// Integer evaluation, matching Java RenderContract exactly for linear and constant tracks and to
// within the documented tolerance for cosine (see render_engine.cpp).
int32_t linearValue(int32_t from, int32_t to, int32_t durationMs, int32_t t);
int32_t cosineValue(int32_t from, int32_t to, int32_t durationMs, int32_t t);
// Value of `track` at `t` ms: before 0 the first `from`, after the end the last `to`; each segment
// owns [start, end).
int32_t trackValueAt(const Definition& def, const Track& track, int64_t t);
// Half-open gate: open when open <= t < close.
inline bool gateOpen(int32_t open, int32_t close, int32_t t) { return t >= open && t < close; }

enum class Activation : uint8_t { Nothing, Activated, Rejected };

class Engine {
 public:
  Engine();

  // Local, assembly-time safety limit; the only way to change it. Server data cannot raise it.
  void setCeiling(uint8_t ceiling) { ceiling_ = ceiling; }
  uint8_t ceiling() const { return ceiling_; }

  // Parses and validates into the inactive buffers. Never touches active state; on error nothing
  // pending remains (a previously pending update is superseded and dropped).
  Error stage(const char* body, size_t len, const char* expectedModel);

  // Render-loop activation point: validates the pending update against the definition that will
  // be in effect, then swaps buffers atomically. On rejection active state is unchanged.
  Activation activate(Error* reason = nullptr);

  bool hasActive() const { return haveDef_ && haveInputs_; }
  const char* activeDigest() const { return activeDigest_; }
  int64_t activeGeneration() const { return generation_; }
  const Definition& activeDefinition() const { return defs_[activeDef_]; }

  // Requested brightness of the active definition clamped to the local ceiling.
  uint8_t effectiveBrightness() const;

  // Renders `count` pixels at monotonic `nowMs` (uint32 wrap tolerated). Without an active
  // configuration, or for unbound pixels, output is off. The shared clock never restarts when a
  // configuration is replaced.
  void render(uint32_t nowMs, Rgb* out, size_t count);

 private:
  enum : int { IDLE = 0, STAGING = 1, READY = 2, ACTIVATING = 3 };
  Definition defs_[2];
  Inputs ins_[2];
  Envelope stagedMeta_;
  int activeDef_;
  int activeIn_;
  bool haveDef_;
  bool haveInputs_;
  char activeDigest_[DIGEST_HEX_LEN + 1];
  int64_t generation_;
  uint8_t ceiling_;
  std::atomic<int> state_;
  uint32_t lastMs_;
  uint64_t elapsedMs_;
};

}  // namespace KiteWindRender

#endif
