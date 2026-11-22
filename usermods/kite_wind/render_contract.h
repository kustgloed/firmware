// SPDX-License-Identifier: EUPL-1.2
// Copyright (c) 2026 Chura IT Consultancy (Kustgloed)

#ifndef KITE_WIND_RENDER_CONTRACT_H
#define KITE_WIND_RENDER_CONTRACT_H

// Device render contract version 1: bounded, integer-only parser/validator for the
// `GET /device/render` envelope. Portable C++11, no allocation, no external libraries.
// The JSON tokenizer is private to render_contract.cpp; only parseEnvelope() is exposed, so the
// parser can later be swapped (for example for ArduinoJson) without touching the engine.
//
// Wire format (as serialized by Jackson from DeviceRenderService.Envelope; field order is not
// significant, unknown fields are skipped, null equals absent):
//   {"contractVersion":1,"modelId":"nl-51-v1","configurationDigest":"<64 lowercase hex>",
//    "inputGeneration":123,"serverTime":"2026-11-15T10:00:00Z",       // serverTime is ignored
//    "definition":{                                                    // omitted on digest match
//      "contractVersion":1,"modelId":"nl-51-v1",
//      "palette":[0,255,...],                                          // RGB ints 0..0xFFFFFF
//      "tracks":[{"id":0,"kind":"CONSTANT|LINEAR|COSINE",
//                 "segments":[{"durationMs":1750,"from":0,"to":50}]}],
//      "gates":[{"clockRef":0,"open":0,"close":3500}],
//      "clocks":[{"id":0,"periodMs":5000,"offsetMs":0}],
//      "brightness":255},
//    "inputs":{"generation":123,
//              "bindings":[{"pixel":0,"trackRef":0,"paletteRef":0}]}}
// Integers only: fractions, exponents, and numbers outside the field range are rejected.

#include <stddef.h>
#include <stdint.h>

namespace KiteWindRender {

constexpr int CONTRACT_VERSION = 1;
// Frozen ingress ceilings (Java RenderContract).
constexpr size_t MAX_BODY_BYTES = 16 * 1024;
constexpr size_t MAX_TRACKS = 96;
constexpr size_t MAX_SEGMENTS = 512;
constexpr size_t MAX_PIXELS = 52;  // pixel bindings; pixel index is 0..51
constexpr size_t MAX_PALETTE = 256;
constexpr int MAX_DEPTH = 8;
constexpr int32_t MAX_DURATION_MS = 86400000;
constexpr int32_t MAX_VALUE = 1000000;
// Firmware-side ceilings for lists the Java contract leaves unbounded.
constexpr size_t MAX_GATES = 96;
constexpr size_t MAX_CLOCKS = 16;
constexpr size_t MAX_MODEL_ID = 32;   // including terminator
constexpr size_t DIGEST_HEX_LEN = 64;

enum class Kind : uint8_t { Constant = 0, Linear = 1, Cosine = 2 };

enum class Error : uint8_t {
  Ok = 0,
  TooLarge,            // body above 16 KiB
  Malformed,           // JSON syntax, wrong type, trailing data, bad string
  DepthExceeded,       // nesting deeper than 8
  NonInteger,          // fraction/exponent where an integer is required
  Overflow,            // integer does not fit its field
  MissingField,
  DuplicateKey,
  UnsupportedVersion,  // contractVersion != 1
  ModelMismatch,       // model id differs from this device's model
  BadDigest,           // not 64 lowercase hex characters
  TooManyTracks,
  TooManySegments,
  TooManyBindings,
  TooManyPalette,
  TooManyGates,
  TooManyClocks,
  ZeroDuration,        // segment duration or clock period <= 0
  OutOfRange,          // value, brightness, colour, offset, gate window out of range
  BadKind,
  BadId,               // track/clock id differs from its index
  BadReference,        // unknown clock, track, or palette entry
  PixelOutOfRange,
  DuplicatePixel,
  Busy,                // engine only: staging slot is being activated
  NoDefinition,        // engine only: inputs staged with no definition to render them
};

const char* errorName(Error e);

struct Segment {
  int32_t durationMs;
  int32_t from;
  int32_t to;
};

struct Track {
  Kind kind;
  uint16_t firstSegment;  // index into Definition::segments
  uint16_t segmentCount;
};

struct Gate {
  uint8_t clockRef;
  int32_t open;   // half-open window [open, close)
  int32_t close;
};

struct Clock {
  int32_t periodMs;
  int32_t offsetMs;
};

// Stable, digest-covered part. Fixed capacity (about 9 KiB), zero allocation.
struct Definition {
  uint16_t brightness;  // 0..255, requested; the local ceiling can only lower it
  uint16_t paletteCount;
  uint16_t trackCount;
  uint16_t segmentCount;
  uint16_t gateCount;
  uint16_t clockCount;
  uint32_t palette[MAX_PALETTE];
  Track tracks[MAX_TRACKS];
  Segment segments[MAX_SEGMENTS];
  Gate gates[MAX_GATES];
  Clock clocks[MAX_CLOCKS];
};

struct Binding {
  uint8_t pixel;
  uint8_t trackRef;
  uint8_t paletteRef;
};

// Volatile per-pixel assignment. Pixels without a binding stay off.
struct Inputs {
  int64_t generation;
  uint16_t bindingCount;
  Binding bindings[MAX_PIXELS];
};

// Envelope metadata; the definition and inputs land in caller-supplied fixed buffers.
struct Envelope {
  int32_t contractVersion;
  char modelId[MAX_MODEL_ID];
  char digest[DIGEST_HEX_LEN + 1];
  int64_t inputGeneration;
  bool hasDefinition;
  bool hasInputs;
};

// `expectedModel` must be shorter than MAX_MODEL_ID (checked at run time: Malformed otherwise).
// Parses and fully validates `body`. On Ok, `meta`, and `def`/`inputs` when flagged present, are
// complete. On any other result the outputs are unspecified and must not be used. Cross references
// between inputs and the definition are checked here only when both are present.
Error parseEnvelope(const char* body, size_t len, const char* expectedModel, Envelope& meta,
                    Definition& def, Inputs& inputs);

// Checks every binding's track and palette reference against `def` (BadReference on failure).
Error validateInputs(const Definition& def, const Inputs& inputs);

}  // namespace KiteWindRender

#endif
