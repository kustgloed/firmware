// SPDX-License-Identifier: EUPL-1.2
// Copyright (c) 2026 Chura IT Consultancy (Kustgloed)

#include "render_contract.h"

#include <string.h>

namespace KiteWindRender {

const char* errorName(Error e) {
  switch (e) {
    case Error::Ok: return "Ok";
    case Error::TooLarge: return "TooLarge";
    case Error::Malformed: return "Malformed";
    case Error::DepthExceeded: return "DepthExceeded";
    case Error::NonInteger: return "NonInteger";
    case Error::Overflow: return "Overflow";
    case Error::MissingField: return "MissingField";
    case Error::DuplicateKey: return "DuplicateKey";
    case Error::UnsupportedVersion: return "UnsupportedVersion";
    case Error::ModelMismatch: return "ModelMismatch";
    case Error::BadDigest: return "BadDigest";
    case Error::TooManyTracks: return "TooManyTracks";
    case Error::TooManySegments: return "TooManySegments";
    case Error::TooManyBindings: return "TooManyBindings";
    case Error::TooManyPalette: return "TooManyPalette";
    case Error::TooManyGates: return "TooManyGates";
    case Error::TooManyClocks: return "TooManyClocks";
    case Error::ZeroDuration: return "ZeroDuration";
    case Error::OutOfRange: return "OutOfRange";
    case Error::BadKind: return "BadKind";
    case Error::BadId: return "BadId";
    case Error::BadReference: return "BadReference";
    case Error::PixelOutOfRange: return "PixelOutOfRange";
    case Error::DuplicatePixel: return "DuplicatePixel";
    case Error::Busy: return "Busy";
    case Error::NoDefinition: return "NoDefinition";
  }
  return "Unknown";
}

Error validateInputs(const Definition& def, const Inputs& inputs) {
  for (size_t i = 0; i < inputs.bindingCount; i++) {
    if (inputs.bindings[i].trackRef >= def.trackCount ||
        inputs.bindings[i].paletteRef >= def.paletteCount)
      return Error::BadReference;
  }
  return Error::Ok;
}

namespace {

// Tiny bounded JSON reader. Recursion depth is capped at MAX_DEPTH, so stack use is constant.
struct Reader {
  const char* p;
  const char* end;
  int depth;
  Error err;
};

bool fail(Reader& r, Error e) {
  if (r.err == Error::Ok) r.err = e;
  return false;
}

void skipWs(Reader& r) {
  while (r.p < r.end && (*r.p == ' ' || *r.p == '\t' || *r.p == '\n' || *r.p == '\r')) r.p++;
}

bool eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Reads a string at r.p (must be '"'). Copies up to cap-1 chars into buf (may be null);
// `truncated` is set when the string was longer. Escapes are decoded; \uXXXX becomes '?'.
bool readString(Reader& r, char* buf, size_t cap, bool& truncated) {
  truncated = false;
  if (r.p >= r.end || *r.p != '"') return fail(r, Error::Malformed);
  r.p++;
  size_t n = 0;
  for (;;) {
    if (r.p >= r.end) return fail(r, Error::Malformed);
    unsigned char c = static_cast<unsigned char>(*r.p++);
    char out;
    if (c == '"') break;
    if (c < 0x20) return fail(r, Error::Malformed);
    if (c == '\\') {
      if (r.p >= r.end) return fail(r, Error::Malformed);
      char e = *r.p++;
      switch (e) {
        case '"': out = '"'; break;
        case '\\': out = '\\'; break;
        case '/': out = '/'; break;
        case 'b': out = '\b'; break;
        case 'f': out = '\f'; break;
        case 'n': out = '\n'; break;
        case 'r': out = '\r'; break;
        case 't': out = '\t'; break;
        case 'u':
          for (int i = 0; i < 4; i++) {
            if (r.p >= r.end || hexVal(*r.p) < 0) return fail(r, Error::Malformed);
            r.p++;
          }
          out = '?';
          break;
        default: return fail(r, Error::Malformed);
      }
    } else {
      out = static_cast<char>(c);
    }
    if (buf && n + 1 < cap) buf[n++] = out;
    else truncated = true;
  }
  if (buf && cap) buf[n] = '\0';
  return true;
}

// Integer token. Rejects fractions/exponents (NonInteger), leading zeros and a bare '-'
// (Malformed), and magnitudes beyond int64 (Overflow).
bool readInt64(Reader& r, int64_t& out) {
  skipWs(r);
  bool neg = false;
  if (r.p < r.end && *r.p == '-') {
    neg = true;
    r.p++;
  }
  if (r.p >= r.end || *r.p < '0' || *r.p > '9') return fail(r, Error::Malformed);
  if (*r.p == '0' && r.p + 1 < r.end && r.p[1] >= '0' && r.p[1] <= '9')
    return fail(r, Error::Malformed);
  uint64_t mag = 0;
  bool overflow = false;
  while (r.p < r.end && *r.p >= '0' && *r.p <= '9') {
    unsigned d = static_cast<unsigned>(*r.p - '0');
    if (mag > (UINT64_MAX - d) / 10) overflow = true;
    else mag = mag * 10 + d;
    r.p++;
  }
  if (r.p < r.end && (*r.p == '.' || *r.p == 'e' || *r.p == 'E')) return fail(r, Error::NonInteger);
  const uint64_t limit = neg ? (static_cast<uint64_t>(INT64_MAX) + 1) : INT64_MAX;
  if (overflow || mag > limit) return fail(r, Error::Overflow);
  out = neg ? static_cast<int64_t>(0 - mag) : static_cast<int64_t>(mag);
  return true;
}

// Integer constrained to [lo, hi]; outside gives `rangeErr`.
bool readInt(Reader& r, int64_t lo, int64_t hi, Error rangeErr, int64_t& out) {
  if (!readInt64(r, out)) return false;
  if (out < lo || out > hi) return fail(r, rangeErr);
  return true;
}
bool readI32(Reader& r, int64_t& out) {  // any int32, otherwise Overflow
  return readInt(r, INT32_MIN, INT32_MAX, Error::Overflow, out);
}

bool expect(Reader& r, char c) {
  skipWs(r);
  if (r.p >= r.end || *r.p != c) return fail(r, Error::Malformed);
  r.p++;
  return true;
}

bool enter(Reader& r, char open) {
  if (!expect(r, open)) return false;
  if (++r.depth > MAX_DEPTH) return fail(r, Error::DepthExceeded);
  return true;
}

// Iterates object members: 1 = key available, 0 = object closed, -1 = error.
int nextKey(Reader& r, bool first, char* key, size_t cap, bool& unknown) {
  skipWs(r);
  if (r.p >= r.end) return fail(r, Error::Malformed) ? 0 : -1;
  if (*r.p == '}' && first) {
    r.p++;
    r.depth--;
    return 0;
  }
  if (!first) {
    if (*r.p == '}') {
      r.p++;
      r.depth--;
      return 0;
    }
    if (*r.p != ',') return fail(r, Error::Malformed) ? 0 : -1;
    r.p++;
    skipWs(r);
  }
  if (!readString(r, key, cap, unknown)) return -1;
  if (!expect(r, ':')) return -1;
  skipWs(r);
  return 1;
}

// Array elements: 1 = element follows, 0 = closed, -1 = error.
int nextElem(Reader& r, bool first) {
  skipWs(r);
  if (r.p >= r.end) return fail(r, Error::Malformed) ? 0 : -1;
  if (*r.p == ']' && first) {
    r.p++;
    r.depth--;
    return 0;
  }
  if (!first) {
    if (*r.p == ']') {
      r.p++;
      r.depth--;
      return 0;
    }
    if (*r.p != ',') return fail(r, Error::Malformed) ? 0 : -1;
    r.p++;
    skipWs(r);
  }
  return 1;
}

bool literal(Reader& r, const char* word) {
  size_t n = strlen(word);
  if (static_cast<size_t>(r.end - r.p) < n || memcmp(r.p, word, n) != 0)
    return fail(r, Error::Malformed);
  r.p += n;
  return true;
}

// Skips any JSON value (unknown fields), validating its syntax.
bool skipValue(Reader& r) {
  skipWs(r);
  if (r.p >= r.end) return fail(r, Error::Malformed);
  char c = *r.p;
  if (c == '"') {
    bool t;
    return readString(r, nullptr, 0, t);
  }
  if (c == '{') {
    if (!enter(r, '{')) return false;
    char key[2];
    bool unk, first = true;
    for (;;) {
      int k = nextKey(r, first, key, sizeof(key), unk);
      if (k < 0) return false;
      if (k == 0) return r.err == Error::Ok;
      first = false;
      if (!skipValue(r)) return false;
    }
  }
  if (c == '[') {
    if (!enter(r, '[')) return false;
    bool first = true;
    for (;;) {
      int k = nextElem(r, first);
      if (k < 0) return false;
      if (k == 0) return r.err == Error::Ok;
      first = false;
      if (!skipValue(r)) return false;
    }
  }
  if (c == 't') return literal(r, "true");
  if (c == 'f') return literal(r, "false");
  if (c == 'n') return literal(r, "null");
  if (c == '-' || (c >= '0' && c <= '9')) {  // full JSON number grammar, value ignored
    if (c == '-') r.p++;
    if (r.p >= r.end || *r.p < '0' || *r.p > '9') return fail(r, Error::Malformed);
    if (*r.p == '0') r.p++;
    else while (r.p < r.end && *r.p >= '0' && *r.p <= '9') r.p++;
    if (r.p < r.end && *r.p == '.') {
      r.p++;
      if (r.p >= r.end || *r.p < '0' || *r.p > '9') return fail(r, Error::Malformed);
      while (r.p < r.end && *r.p >= '0' && *r.p <= '9') r.p++;
    }
    if (r.p < r.end && (*r.p == 'e' || *r.p == 'E')) {
      r.p++;
      if (r.p < r.end && (*r.p == '+' || *r.p == '-')) r.p++;
      if (r.p >= r.end || *r.p < '0' || *r.p > '9') return fail(r, Error::Malformed);
      while (r.p < r.end && *r.p >= '0' && *r.p <= '9') r.p++;
    }
    return true;
  }
  return fail(r, Error::Malformed);
}

// True (and consumes it) when the next value is the literal null.
bool takeNull(Reader& r) {
  skipWs(r);
  if (r.end - r.p >= 4 && memcmp(r.p, "null", 4) == 0) {
    r.p += 4;
    return true;
  }
  return false;
}

bool markSeen(Reader& r, uint32_t& seen, unsigned bit) {
  if (seen & (1u << bit)) return fail(r, Error::DuplicateKey);
  seen |= 1u << bit;
  return true;
}

const size_t KEY_CAP = 24;

bool parseSegment(Reader& r, Definition& d, int64_t& trackTotal) {
  if (d.segmentCount >= MAX_SEGMENTS) return fail(r, Error::TooManySegments);
  if (!enter(r, '{')) return false;
  Segment seg = {0, 0, 0};
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    int64_t v;
    if (!unk && eq(key, "durationMs")) {
      if (!markSeen(r, seen, 0) || !readI32(r, v)) return false;
      if (v <= 0) return fail(r, Error::ZeroDuration);
      if (v > MAX_DURATION_MS) return fail(r, Error::OutOfRange);
      seg.durationMs = static_cast<int32_t>(v);
    } else if (!unk && (eq(key, "from") || eq(key, "to"))) {
      bool isFrom = eq(key, "from");
      if (!markSeen(r, seen, isFrom ? 1 : 2) || !readI32(r, v)) return false;
      if (v < 0 || v > MAX_VALUE) return fail(r, Error::OutOfRange);
      (isFrom ? seg.from : seg.to) = static_cast<int32_t>(v);
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 7) return fail(r, Error::MissingField);
  trackTotal += seg.durationMs;
  if (trackTotal > MAX_DURATION_MS) return fail(r, Error::OutOfRange);
  d.segments[d.segmentCount++] = seg;
  return true;
}

bool parseTrack(Reader& r, Definition& d) {
  if (d.trackCount >= MAX_TRACKS) return fail(r, Error::TooManyTracks);
  if (!enter(r, '{')) return false;
  Track t;
  t.kind = Kind::Constant;
  t.firstSegment = d.segmentCount;
  t.segmentCount = 0;
  int64_t total = 0;
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    if (!unk && eq(key, "id")) {
      int64_t v;
      if (!markSeen(r, seen, 0) || !readI32(r, v)) return false;
      if (v != d.trackCount) return fail(r, Error::BadId);
    } else if (!unk && eq(key, "kind")) {
      char name[12];
      bool trunc;
      if (!markSeen(r, seen, 1) || !readString(r, name, sizeof(name), trunc)) return false;
      if (!trunc && eq(name, "CONSTANT")) t.kind = Kind::Constant;
      else if (!trunc && eq(name, "LINEAR")) t.kind = Kind::Linear;
      else if (!trunc && eq(name, "COSINE")) t.kind = Kind::Cosine;
      else return fail(r, Error::BadKind);
    } else if (!unk && eq(key, "segments")) {
      if (!markSeen(r, seen, 2) || !enter(r, '[')) return false;
      bool firstElem = true;
      for (;;) {
        int e = nextElem(r, firstElem);
        if (e < 0) return false;
        if (e == 0) break;
        firstElem = false;
        if (!parseSegment(r, d, total)) return false;
        t.segmentCount++;
      }
      if (r.err != Error::Ok) return false;
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 7) return fail(r, Error::MissingField);
  if (t.segmentCount == 0 || (t.kind == Kind::Constant && t.segmentCount != 1))
    return fail(r, Error::OutOfRange);
  d.tracks[d.trackCount++] = t;
  return true;
}

bool parseGate(Reader& r, Definition& d) {
  if (d.gateCount >= MAX_GATES) return fail(r, Error::TooManyGates);
  if (!enter(r, '{')) return false;
  Gate g = {0, 0, 0};
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    int64_t v;
    if (!unk && eq(key, "clockRef")) {
      if (!markSeen(r, seen, 0) || !readInt(r, 0, 255, Error::Overflow, v)) return false;
      g.clockRef = static_cast<uint8_t>(v);
    } else if (!unk && eq(key, "open")) {
      if (!markSeen(r, seen, 1) || !readI32(r, v)) return false;
      if (v < 0) return fail(r, Error::OutOfRange);
      g.open = static_cast<int32_t>(v);
    } else if (!unk && eq(key, "close")) {
      if (!markSeen(r, seen, 2) || !readI32(r, v)) return false;
      g.close = static_cast<int32_t>(v);
      if (v > MAX_DURATION_MS) return fail(r, Error::OutOfRange);
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 7) return fail(r, Error::MissingField);
  if (g.close <= g.open) return fail(r, Error::OutOfRange);
  d.gates[d.gateCount++] = g;
  return true;
}

bool parseClock(Reader& r, Definition& d) {
  if (d.clockCount >= MAX_CLOCKS) return fail(r, Error::TooManyClocks);
  if (!enter(r, '{')) return false;
  Clock c = {0, 0};
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    int64_t v;
    if (!unk && eq(key, "id")) {
      if (!markSeen(r, seen, 0) || !readI32(r, v)) return false;
      if (v != d.clockCount) return fail(r, Error::BadId);
    } else if (!unk && eq(key, "periodMs")) {
      if (!markSeen(r, seen, 1) || !readI32(r, v)) return false;
      c.periodMs = static_cast<int32_t>(v);
    } else if (!unk && eq(key, "offsetMs")) {
      if (!markSeen(r, seen, 2) || !readI32(r, v)) return false;
      c.offsetMs = static_cast<int32_t>(v);
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 7) return fail(r, Error::MissingField);
  if (c.periodMs <= 0) return fail(r, Error::ZeroDuration);
  if (c.periodMs > MAX_DURATION_MS || c.offsetMs < 0 || c.offsetMs >= c.periodMs)
    return fail(r, Error::OutOfRange);
  d.clocks[d.clockCount++] = c;
  return true;
}

// Parses a JSON array of objects/ints using `elem`, which consumes one element.
bool parseDefinition(Reader& r, const char* model, Definition& d) {
  d.brightness = 0;
  d.paletteCount = d.trackCount = d.segmentCount = d.gateCount = d.clockCount = 0;
  if (!enter(r, '{')) return false;
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    int64_t v;
    if (!unk && eq(key, "contractVersion")) {
      if (!markSeen(r, seen, 0) || !readI32(r, v)) return false;
      if (v != CONTRACT_VERSION) return fail(r, Error::UnsupportedVersion);
    } else if (!unk && eq(key, "modelId")) {
      char id[MAX_MODEL_ID];
      bool trunc;
      if (!markSeen(r, seen, 1) || !readString(r, id, sizeof(id), trunc)) return false;
      if (trunc || !eq(id, model)) return fail(r, Error::ModelMismatch);
    } else if (!unk && eq(key, "palette")) {
      if (!markSeen(r, seen, 2) || !enter(r, '[')) return false;
      bool firstElem = true;
      for (;;) {
        int e = nextElem(r, firstElem);
        if (e < 0) return false;
        if (e == 0) break;
        firstElem = false;
        if (d.paletteCount >= MAX_PALETTE) return fail(r, Error::TooManyPalette);
        if (!readI32(r, v)) return false;
        if (v < 0 || v > 0xFFFFFF) return fail(r, Error::OutOfRange);
        d.palette[d.paletteCount++] = static_cast<uint32_t>(v);
      }
      if (r.err != Error::Ok) return false;
    } else if (!unk && (eq(key, "tracks") || eq(key, "gates") || eq(key, "clocks"))) {
      int which = eq(key, "tracks") ? 0 : eq(key, "gates") ? 1 : 2;
      if (!markSeen(r, seen, 3 + which) || !enter(r, '[')) return false;
      bool firstElem = true;
      for (;;) {
        int e = nextElem(r, firstElem);
        if (e < 0) return false;
        if (e == 0) break;
        firstElem = false;
        if (!(which == 0 ? parseTrack(r, d) : which == 1 ? parseGate(r, d) : parseClock(r, d)))
          return false;
      }
      if (r.err != Error::Ok) return false;
    } else if (!unk && eq(key, "brightness")) {
      if (!markSeen(r, seen, 6) || !readI32(r, v)) return false;
      if (v < 0 || v > 255) return fail(r, Error::OutOfRange);
      d.brightness = static_cast<uint16_t>(v);
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 0x7F) return fail(r, Error::MissingField);
  if (d.paletteCount == 0) return fail(r, Error::OutOfRange);
  for (size_t i = 0; i < d.gateCount; i++)
    if (d.gates[i].clockRef >= d.clockCount) return fail(r, Error::BadReference);
  return true;
}

bool parseBinding(Reader& r, Inputs& in, uint64_t& usedPixels) {
  if (in.bindingCount >= MAX_PIXELS) return fail(r, Error::TooManyBindings);
  if (!enter(r, '{')) return false;
  Binding b = {0, 0, 0};
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    int64_t v;
    if (!unk && eq(key, "pixel")) {
      if (!markSeen(r, seen, 0) || !readI32(r, v)) return false;
      if (v < 0 || v >= static_cast<int64_t>(MAX_PIXELS)) return fail(r, Error::PixelOutOfRange);
      b.pixel = static_cast<uint8_t>(v);
    } else if (!unk && (eq(key, "trackRef") || eq(key, "paletteRef"))) {
      bool isTrack = eq(key, "trackRef");
      if (!markSeen(r, seen, isTrack ? 1 : 2) || !readInt(r, 0, 255, Error::Overflow, v))
        return false;
      (isTrack ? b.trackRef : b.paletteRef) = static_cast<uint8_t>(v);
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 7) return fail(r, Error::MissingField);
  uint64_t bit = static_cast<uint64_t>(1) << b.pixel;
  if (usedPixels & bit) return fail(r, Error::DuplicatePixel);
  usedPixels |= bit;
  in.bindings[in.bindingCount++] = b;
  return true;
}

bool parseInputs(Reader& r, Inputs& in) {
  in.generation = 0;
  in.bindingCount = 0;
  uint64_t used = 0;
  if (!enter(r, '{')) return false;
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return false;
    if (k == 0) break;
    first = false;
    if (!unk && eq(key, "generation")) {
      int64_t v;
      if (!markSeen(r, seen, 0) || !readInt(r, 0, INT64_MAX, Error::OutOfRange, v)) return false;
      in.generation = v;
    } else if (!unk && eq(key, "bindings")) {
      if (!markSeen(r, seen, 1) || !enter(r, '[')) return false;
      bool firstElem = true;
      for (;;) {
        int e = nextElem(r, firstElem);
        if (e < 0) return false;
        if (e == 0) break;
        firstElem = false;
        if (!parseBinding(r, in, used)) return false;
      }
      if (r.err != Error::Ok) return false;
    } else if (!skipValue(r)) {
      return false;
    }
  }
  if (r.err != Error::Ok) return false;
  if (seen != 3) return fail(r, Error::MissingField);
  return true;
}

}  // namespace

Error parseEnvelope(const char* body, size_t len, const char* expectedModel, Envelope& meta,
                    Definition& def, Inputs& inputs) {
  meta.contractVersion = 0;
  meta.modelId[0] = '\0';
  meta.digest[0] = '\0';
  meta.inputGeneration = 0;
  meta.hasDefinition = meta.hasInputs = false;
  if (len > MAX_BODY_BYTES) return Error::TooLarge;
  if (!body || len == 0 || !expectedModel || strlen(expectedModel) >= MAX_MODEL_ID)
    return Error::Malformed;

  Reader r = {body, body + len, 0, Error::Ok};
  if (!enter(r, '{')) return r.err;
  uint32_t seen = 0;
  char key[KEY_CAP];
  bool unk, first = true;
  for (;;) {
    int k = nextKey(r, first, key, sizeof(key), unk);
    if (k < 0) return r.err;
    if (k == 0) break;
    first = false;
    int64_t v;
    bool ok;
    if (!unk && eq(key, "contractVersion")) {
      ok = markSeen(r, seen, 0) && readI32(r, v);
      if (ok && v != CONTRACT_VERSION) ok = fail(r, Error::UnsupportedVersion);
      if (ok) meta.contractVersion = static_cast<int32_t>(v);
    } else if (!unk && eq(key, "modelId")) {
      bool trunc;
      ok = markSeen(r, seen, 1) && readString(r, meta.modelId, sizeof(meta.modelId), trunc);
      if (ok && (trunc || !eq(meta.modelId, expectedModel))) ok = fail(r, Error::ModelMismatch);
    } else if (!unk && eq(key, "configurationDigest")) {
      bool trunc;
      ok = markSeen(r, seen, 2) && readString(r, meta.digest, sizeof(meta.digest), trunc);
      if (ok) {
        bool good = !trunc && strlen(meta.digest) == DIGEST_HEX_LEN;
        for (size_t i = 0; good && i < DIGEST_HEX_LEN; i++) {
          char c = meta.digest[i];
          good = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        if (!good) ok = fail(r, Error::BadDigest);
      }
    } else if (!unk && eq(key, "inputGeneration")) {
      ok = markSeen(r, seen, 3) && readInt(r, 0, INT64_MAX, Error::OutOfRange, v);
      if (ok) meta.inputGeneration = v;
    } else if (!unk && eq(key, "definition")) {
      ok = markSeen(r, seen, 4);
      if (ok && !takeNull(r)) {
        ok = parseDefinition(r, expectedModel, def);
        if (ok) meta.hasDefinition = true;
      }
    } else if (!unk && eq(key, "inputs")) {
      ok = markSeen(r, seen, 5);
      if (ok && !takeNull(r)) {
        ok = parseInputs(r, inputs);
        if (ok) meta.hasInputs = true;
      }
    } else {
      ok = skipValue(r);  // serverTime and unknown fields
    }
    if (!ok) return r.err == Error::Ok ? Error::Malformed : r.err;
  }
  if (r.err != Error::Ok) return r.err;
  skipWs(r);
  if (r.p != r.end) return Error::Malformed;
  if ((seen & 0xF) != 0xF) return Error::MissingField;
  if (meta.hasDefinition && meta.hasInputs) return validateInputs(def, inputs);
  return Error::Ok;
}

}  // namespace KiteWindRender
