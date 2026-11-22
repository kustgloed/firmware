<!--
SPDX-License-Identifier: EUPL-1.2
Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
-->

# Kite wind

This usermod polls the KiteWindMap Java API over the LAN. It supports ESP8266 and ESP32, maps database spot UUIDs to physical LED indexes, and renders the server-selected wind color and timing.

## Build

Enable `kite_wind` in `custom_usermods`. The local `kite` target uses a D1 mini on GPIO 4. Stored WLED configuration determines the LED count; the new local build defaults to 23 LEDs: one system lamp and 22 spot lamps. The current local target uses 52 LEDs and segment zero start 0 / stop 52, leaving 51 spot lamps after reserving pixel 0. Capacity follows stored WLED settings, not the build default. Building alone does not change stored device settings. The local D1 mini initially used 23 lamps on 2026-10-25; its stored GPIO 2 overrides the build default GPIO 4. Reboot verification confirmed 23 lamps, 22 mappings, and healthy API polling. Run `npm ci`, `npm run build`, `npm test`, and `pio run -e kite`. The local ESP32 validation target also includes the usermod.

If PlatformIO needs a different Python version, install it in a virtual environment and run `python -m platformio run -e kite`.

The same controller was later rebuilt and redeployed with 27 configured lamps and 26 spot mappings. Reboot checks confirmed the saved layout, original 22 mappings, four added mappings, retained credential, GPIO 2, and 1048 mA limit. Runtime segment changes selected 25 then 26 spots without reflashing. Healthy API polling reported system code 0; all weather states were `UNKNOWN` because the API had no snapshots.

The 2026-10-25 wind-color release was built for ESP8266 and ESP32, then uploaded to the existing D1 mini with a verified flash hash. Fresh polling and system code 0 were verified after upload and a second reboot. All 26 mappings, 27 LEDs, segment 0–27, GPIO, strip type, power settings and credential presence matched the pre-upload baseline. The API currently supplies explicitly simulated weather.

## Configure

Start the Java server with the `fake-weather` profile. For a LAN setup, the controller uses the computer's LAN address, never `localhost` or `0.0.0.0`.

The database UUID returned as `id` by `/api/v1/spots` is the sole spot identifier. The controller stores and requests that UUID; provider/station identifiers are not part of the configuration or API contract.

Usermod settings contain:

- `enabled`: enable API polling and LED control.
- `host` and `port`: server IPv4 address or DNS hostname and HTTP port.
- `spots`: up to 51 comma-separated `UUID@LED` entries, with zero-based physical LED indexes. Pixel 0 is reserved for system status and is rejected in spot mappings. Each UUID and pixel must be unique. Only mappings within segment zero's physical start (inclusive) and stop (exclusive) are requested and rendered. The current range 0–52 permits 51 spots on pixels 1–51; there is no fixed lamp count. Segment edits update the selection and invalidate pending responses immediately. Unmapped lamps stay off.
- `key-set`: reports whether a credential is present; changing this value has no effect.

Provision a device credential through the server. Keep the device credential and UUID-to-pixel settings in private local files.

From the Java project, run `python3 scripts/configure-wled.py --device <controller-ip>`. This reads segment zero from `/json/state`, requires its start to be 0, selects spot mappings in its range, and sends settings to `/json/cfg` without displaying the key or putting it in command arguments. For a PIN-protected controller, supply `WLED_SETTINGS_PIN` through the environment. The helper requires the new firmware.

`api-key` is a write-only property accepted in the `um.KiteWind` JSON configuration object. An empty value clears it; omitting it preserves the existing key. It is not a public usermod field. The credential persists only in WLED's protected `wsec.json`; public settings, backups, and info responses omit it. Usermod secret hooks in WLED's configuration layer preserve it across reboot and normal saves.

## System lamp

Physical pixel 0 (the first lamp) shows solid green after a complete successful API polling cycle while results remain fresh and no selected spot reports `UNKNOWN`. It communicates system faults through counted red blinks. The status lamp remains available even if segment zero accidentally excludes pixel 0. Faults blink red in counted groups: 200 ms on, 200 ms off per blink, then a two-second pause. Groups repeat while the fault remains; normal polling continues without blocking. Only this lamp blinks an error code.

| Blinks | Meaning |
| --- | --- |
| 1 | Server unreachable, timeout, or waiting for the first complete response |
| 2 | Wi-Fi/network disconnected |
| 3 | Missing server/key/spot settings or unsuitable segment zero |
| 4 | API rejects credentials (HTTP 401/403) |
| 5 | Other non-200 HTTP response (not 400, 401, 403, 404, or 5xx) |
| 6 | Invalid, incomplete, oversized, or unexpected API response |
| 7 | Last complete weather data is stale: 60 seconds old on ESP8266 and other builds, 30 minutes on ESP32-S3 |
| 8 | API responds, but weather data is unavailable (`UNKNOWN`) for one or more selected spots |
| 9 | HTTP 404: a mapped spot UUID is missing from the server catalogue |
| 10 | HTTP 400: the server rejects the request as invalid |
| 11 | HTTP 5xx: server-side failure (for example database unavailable) |
| 12 | TLS verification failed or system clock unusable for HTTPS |

HTTPS (ESP32-S3, port 443) waits for WLED's own NTP time. Enable NTP in WLED's time settings; without it code 12 persists and no HTTPS poll starts.

Setup and network faults take priority. Otherwise the latest API error remains until a complete successful polling cycle; code 7 applies when no more specific API error exists. Valid `NONE` means known unsuitable conditions and remains healthy. `UNKNOWN` keeps that spot lamp off and reports code 8; known results for other spots still display normally. An empty weather cache therefore cannot show a green system lamp. The WLED info response exposes `Kite system blink code` and the spot capacity excluding pixel 0. Debug logs report changes to the numeric status code without connection settings or credentials. Disabling the usermod or a realtime override also suspends the status lamp.

The current setup uses consecutive pixels 1–26 in the supplied table's order. Pixel-0 spot configurations are rejected. The helper does not change the physical LED count or segment range.

## LED behavior

The API stores absolute wind bands in PostgreSQL and returns the complete `displayConfig` with every response. Each spot result includes its resolved RGB `color` and `periodMs`; the controller does not evaluate weather or assign colors by forecast age.

| Wind speed (knots) | Default color |
| --- | --- |
| Below 10 | Off (`#000000`) |
| 10 to below 15 | Blue (`#0000FF`) |
| 15 to below 21 | Green (`#00FF00`) |
| 21 to below 26 | Yellow (`#FFFF00`) |
| 26 to below 31 | Orange (`#FF8000`) |
| 31 and above | Red (`#FF0000`) |

| API state | Timing |
| --- | --- |
| `NOW` | Suitable current conditions in daylight, or the earliest suitable forecast in today's remaining daylight; server color, solid at 80% in both effects |
| `WITHIN_42_HOURS` | Strongest suitable forecast through 42 hours, fade every 5 seconds at up to 50% |
| `WITHIN_7_DAYS` | Strongest suitable forecast after 42 through 168 hours, fade every 5 seconds at up to 30% |
| `NONE`, `UNKNOWN`, stale results | Off |

The server applies one selection policy to both effects: `NOW` first, then the earliest suitable forecast. Through 42 elapsed hours inclusive uses the near fade (nighttime forecasts allowed); later samples through 168 hours inclusive use the seven-day fade. Suitability covers speed band, direction sector, and allowed month. The server decides daylight (NOAA-style, per spot); the monthly night-dimming table below only dims brightness. Without a match, the server returns `NONE` or `UNKNOWN` with black and zero period. The controller displays that result; it does not repeat suitability or calendar evaluation.

The server display configuration selects a named effect. The supported effects are exactly `default` and `rhythm`; they change presentation only. `default` uses the behavior below; the effect name is required. Unsupported names reject the response, leaving previous results to expire. Future effects require firmware support before selection.

Forecast lamps start a smooth fade together using the controller clock. Both default cycles repeat every five seconds. The 42-hour lamps fade in and out over 3.5 seconds; seven-day lamps fade in and out over two seconds. They stay off for the remainder of the cycle. Fade durations are 70% and 40% of their respective configured periods. Updates occur every 50 ms. Black and expired results stay off. Server SQL updates change colors and timing on subsequent complete polling cycles without reflashing. Existing mappings, settings and protected credentials remain device-specific.

## Polling by platform

| Platform | Transport | Cadence |
| --- | --- | --- |
| ESP8266 and other builds | LAN HTTP, batches of four spots | Every 10 s; results older than 60 s turn the spot lamps off |
| ESP32-S3 with PSRAM | HTTPS on port 443 from a worker task, one batch of up to 51 spots | Every 10 min after a success; retries after 30 s, 1 min, 2 min, then every 5 min; HTTP 401 retries after 10 min |

- The S3 build pins the ISRG Root X1 certificate (`isrg_root_x1.h`) and refuses plain HTTP to a public host (code 3). Plain HTTP stays valid for LAN addresses and local names.
- Builds without HTTPS refuse port 443.
- The hold timer keeps the last complete result for 30 minutes after the last success. The spot lamps keep the held result, then turn off. A failed poll shows its error code at once. A connection failure gives code 1.
- The info panel (`/json/info`, section `u`) adds `Kite successful polls` on every build. S3 builds also report `Kite TLS errors`, `Kite worker stack free` (bytes) and `Kite min internal heap` (bytes).

The details below describe the 10-second LAN client (ESP8266 and other builds) unless stated otherwise.

Polling runs every ten seconds with an eight-second TCP request deadline. A timed-out TCP PCB is cancelled on its owning lwIP context; pending DNS must retire before request memory is reused. Invalid responses retain the last result for at most 60 seconds, then spot LEDs turn off. The info panel distinguishes simulated weather and shows response age. Disabling the usermod releases realtime LED ownership.

Requests use `/api/v1/led-conditions` over HTTP/1.0 with close-delimited compact responses. Selected spots are fetched in batches of at most four, with a 2304-byte response and 704-byte request buffer. ESP32-S3 builds with PSRAM (`CONFIG_IDF_TARGET_ESP32S3` and `BOARD_HAS_PSRAM`) fetch up to 51 spots per batch with a 16384-byte response and 2560-byte request buffer; `-DKITE_WIND_BATCH_SIZE=<n>` overrides the size. The batch size is the transport size, not the number of spots displayed. All selected results are published together after a successful complete cycle. A failed batch discards that cycle and leaves the previous complete result to expire. Batches of four (ESP8266 and other targets) or 51 (ESP32-S3 with PSRAM) keep the response bounded while carrying the display configuration; other firmware targets reject batches above four at compile time. JSON parsing stays bounded on ESP8266 while supporting the full segment selection. Unexpected status codes, malformed JSON, missing/duplicate spot results, and unknown states do not replace the last valid result. LAN HTTP needs a trusted LAN. The ESP32-S3 build uses verified HTTPS for production. Device authentication stays enabled on the Java server.

## Monthly night dimming

All KiteWind lamps, including the system lamp, use 70% of their state-adjusted output from the monthly sunset minute until the sunrise minute. During daytime, solid spot lamps in either effect use 80% of configured brightness; default-effect 42-hour and seven-day lamps use 50% and 30%. Night dimming multiplies these levels by 70%. Manual brightness changes remain the baseline; the schedule never overwrites saved brightness. Existing colors, fade periods, error blink timing, and realtime overrides remain in effect.

The firmware stores 12 approximate Amsterdam sunrise/sunset pairs based on the 15th of each month in 2026. The table in `night_dimming.h` uses UTC minutes and repeats each year. Amsterdam summer/winter time rules select the local month and give the appropriate local switching times. Actual sun times vary within each month. No sun-time service or download is used. A valid clock is still required, normally through WLED NTP; before synchronization, output remains at configured brightness. The feature applies only while KiteWind owns the lamps.

Native validation: `c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-night-dimming-test.cpp -o /tmp/kite-wind-night-dimming-test && /tmp/kite-wind-night-dimming-test`.

## Native validation

Native blink timing validation: `c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-status-test.cpp -o /tmp/kite-wind-status-test && /tmp/kite-wind-status-test`.

Native poll policy validation (retry schedule, hold timer, host classification):

```sh
c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-poll-policy-test.cpp -o /tmp/kite-wind-poll-policy-test && /tmp/kite-wind-poll-policy-test
```

Native HTTP exchange validation (51-spot request and response bounds):

```sh
c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-http-test.cpp usermods/kite_wind/display_contract.cpp -o /tmp/kite-wind-http-test && /tmp/kite-wind-http-test
```

Native display validation uses the bundled JSON parser in both host and embedded modes:

```sh
c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-display-test.cpp usermods/kite_wind/display_contract.cpp -o /tmp/kite-wind-display-test
/tmp/kite-wind-display-test
c++ -std=c++11 -Wall -Wextra -Werror -DARDUINOJSON_EMBEDDED_MODE=1 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 tools/kite-wind-display-test.cpp usermods/kite_wind/display_contract.cpp -o /tmp/kite-wind-display-embedded-test
/tmp/kite-wind-display-embedded-test
```

Repeat both display builds with `-DKITE_WIND_BATCH_SIZE=51` to check the ESP32-S3 batch; parsing rules are identical for both sizes.

The display parser lives in its own translation unit to preserve full server double thresholds without changing WLED's JSON types. A bounded token pass uses `strtod` before ArduinoJson mutates the input; native tests cover adjacent representable thresholds.

## Rhythm effect

Select `effect: "rhythm"` in PostgreSQL `display_settings.configuration` after deploying API and firmware support. The `default` effect remains available.

An optional top-level `lampTest` object (`periodMs`, `colors`; sent only by the API `lamp-test` profile) makes every `NOW` lamp show `colors[(millis / periodMs) % count]`, so all lamps change together; other states and night dimming apply as usual.

`displayConfig.rhythm` contains:

```json
{
  "now": {"pattern": "SOLID", "periodMs": 1200, "brightness": 255},
  "within42Hours": {"pattern": "DOUBLE_FLASH", "periodMs": 2400, "brightness": 140},
  "within7Days": {"pattern": "SLOW_PULSE", "periodMs": 5000, "brightness": 140},
  "forecastShowMs": 5000,
  "forecastTailMs": 1000,
  "forecastPauseMs": 4000
}
```

The fixed local timer shows both forecast patterns for five seconds, near forecasts alone for one second, then pauses for four seconds. NOW stays solid at 80% of configured WLED brightness, regardless of the `SOLID` pattern brightness setting. Two smooth 400 ms fades separated by 200 ms form the double flash at its default 2400 ms period; the rest is dark. Both forecast patterns peak at 55% brightness (140/255). Slow pulses use a cosine fade. Animated pattern brightness is relative to configured WLED brightness, with existing night dimming applied afterwards.

The API applies the shared selection policy: `NOW`, then the strongest suitable covered forecast through 42 hours (`WITHIN_42_HOURS`) or after 42 through 168 hours (`WITHIN_7_DAYS`). The `now` pattern must be `SOLID`; Flyway V29 renamed the stored `within2Days` key. Colors retain the configured wind bands. Per-spot `periodMs` matches the selected pattern, while NOW retains zero.

Complete API updates are staged for the next cycle start; NOW updates immediately. Polling does not restart the timer. Stale responses expire after 60 seconds (30 minutes on ESP32-S3), including staged forecast snapshots. Pixel 0 remains reserved for system status. `/json/state` exposes read-only `kiteDisplay` diagnostics. SQL changes take effect without reflashing; changing cycle settings takes effect at a cycle boundary.

The rhythm cycle totals ten seconds (5000 ms show, 1000 ms tail, 4000 ms pause). The seven-day pulse lasts exactly the five-second show window, so its fade-out completes before the window closes.

## Render contract v1 (clean-slate controller, skeleton)

`render_contract.{h,cpp}` and `render_engine.{h,cpp}` are the host-tested parser and generic player for `GET /device/render` (version 1). They are not yet wired into `kite_wind.cpp`; the LAN polling path above is unchanged until cutover. PlatformIO compiles every `.cpp` in this folder, so `library.json` needs no change.

Native test (UBSan clean with `-fsanitize=undefined`; ASan not run, it hung at startup in the development environment):

```sh
c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-render-test.cpp usermods/kite_wind/render_contract.cpp usermods/kite_wind/render_engine.cpp -o /tmp/kite-wind-render-test
/tmp/kite-wind-render-test
```

Wire format (Jackson output of `DeviceRenderService.Envelope`; `serverTime` is an ISO-8601 string and is ignored; unknown fields are skipped, `null` equals absent; `definition` is omitted when the sent digest matches):

```json
{"contractVersion":1,"modelId":"nl-51-v1","configurationDigest":"<64 lowercase hex>",
 "inputGeneration":1700000000123,"serverTime":"2026-11-15T10:00:00Z",
 "definition":{"contractVersion":1,"modelId":"nl-51-v1",
   "palette":[0,255,65280,16776960,16744448,16711680],
   "tracks":[{"id":2,"kind":"COSINE","segments":[{"durationMs":1750,"from":0,"to":50}]}],
   "gates":[{"clockRef":0,"open":0,"close":3500}],
   "clocks":[{"id":0,"periodMs":5000,"offsetMs":0}],"brightness":255},
 "inputs":{"generation":1700000000123,"bindings":[{"pixel":1,"trackRef":1,"paletteRef":2}]}}
```

Ceilings: body 16 KiB, 96 tracks, 512 segments (unreachable inside 16 KiB; at least 32 bytes each), 52 bindings (pixel 0..51), palette 256, depth 8, durations 1..86 400 000 ms, values 0..1 000 000. Firmware-only ceilings where Java is unbounded: 96 gates, 16 clocks. Everything is integer-only; unknown `kind`, mismatched ids, duplicate keys or pixels, zero durations, overflow, and bad references are rejected. A failed parse or activation never changes the active configuration.

Evaluation: track value is a percent (0..100, clamped) of the effective brightness `min(server brightness, local ceiling)`; channel = round_half_up(colour * brightness * percent / 25 500). Each track loops over its own total duration on the shared elapsed clock (clock 0 offset). Linear is bit-identical to Java; cosine uses a Q30 integer sine series and equals Java's double cosine except on exact rounding ties (values within about 2e-9 of a tie, so deltas below 255 differ only at ties such as t/d = 1/2). Gates are validated and `gateOpen()` is available, but v1 bindings carry no gate reference, so rendering does not apply them yet.

Known gaps: gates and non-zero clocks are parsed and validated but not applied to rendering (the contract needs a track-to-clock link before the full engine). Track values are percent 0..100 of the effective brightness.
