<!--
SPDX-License-Identifier: EUPL-1.2
Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
-->

# Kite wind

This usermod polls `GET /device/render` on the wind map server and plays the effect from the response. The server selects colors, animations, brightness and the lamp of each spot. The controller does not evaluate weather.

## Build

Enable `kite_wind` in `custom_usermods`. The supported controller build is `kite_c3` (ESP32-C3). The release check also builds `kite_s3`, `kite` (ESP8266) and `esp32dev`. Run `npm ci`, `npm run build`, `npm test`, and `pio run -e kite_c3`.

If PlatformIO needs a different Python version, install it in a virtual environment and run `python -m platformio run -e kite_c3`.

Building alone does not change stored device settings. The LED count, GPIO, strip type and power limit come from the stored WLED configuration.

The `kite_c3` environment targets ESP32-C3 with 4 MB flash. It uses `tools/partitions-kite-c3.csv`: two 1.875 MiB OTA slots and a 192 KiB filesystem. The partition table cannot change over OTA, so an existing C3 controller needs one USB reflash. The reflash wipes presets and `cfg.json`. The environment switches off MQTT, Alexa, Hue sync, infrared, GIF and DMX input. Judge the image size by the real `firmware.bin` against the 1,966,080-byte slot, not by the PlatformIO "Flash used" figure.

## Configure

Usermod settings (`um.KiteWind`) contain:

- `enabled`: enable API polling and LED control.
- `host` and `port`: server IPv4 address or DNS name and port. Port 443 selects HTTPS.
- `api-key`: the device key. It is write-only. An empty value clears it, and omitting it keeps the saved key.
- `key-set`: reports whether a key is present; changing this value has no effect.

The server assigns lamps 1 to 51 to spots, so the controller needs no spot list. Pixel 0 is the status lamp. The physical LED count is 52, and segment zero must start at 0. Spot lamps outside segment zero stay unused.

The key persists only in WLED's protected `wsec.json`. Public settings, backups and info responses omit it. Provision the key through the server and keep it in private local files. The server project's `scripts/configure-wled.py` sends the settings to `/json/cfg` without printing the key. For a PIN-protected controller, supply `WLED_SETTINGS_PIN` through the environment.

## Polling

Each poll is one request: `GET /device/render?contract=2&model=nl-51-v1[&digest=<64 hex>]`. The digest names the active definition. A controller without a definition sends none. The request uses HTTP/1.0, `Connection: close`, a Bearer key and `X-Firmware-Version`.

| Platform | Transport | Cadence | Hold after the last success |
| --- | --- | --- | --- |
| ESP32-C3 (`kite_c3`) and ESP32-S3 with PSRAM | HTTPS on port 443, or plain HTTP for LAN hosts, from a worker task | Every 10 min after a success; retries after 30 s, 1 min, 2 min, then every 5 min; HTTP 401 retries after 10 min | 30 min |
| ESP8266 and classic ESP32 | Plain HTTP for LAN hosts | Every 10 s, with an 8 s request deadline | 60 s |

- HTTPS builds pin the ISRG Root X1 certificate (`isrg_root_x1.h`) and refuse plain HTTP to a public host (code 3). ESP8266 and classic ESP32 refuse port 443.
- Plain HTTP works only for private IPv4 addresses, `localhost` and `.local` names. The Kustgloed server refuses plain HTTP on this route, so it needs port 443.
- One TLS session serves each poll. A heap guard fails the poll (code 1) when free internal heap is below 40960 bytes or the largest free block is below 20480 bytes (`MIN_FREE_HEAP_BYTES`, `MIN_LARGEST_BLOCK_BYTES` in `http_exchange.h`).
- Each exchange has a 20-second acceptance deadline. Connect, handshake and stalled reads or writes are limited to 8 seconds.
- Every request carries `X-Firmware-Version` (`<VERSION>+<short commit>.<environment>`, at most 64 characters of `[0-9A-Za-z._+-]`). The ESP32-C3 build gets it from `tools/kite_version.py` and the `VERSION` file; other builds send `0.0.0+unknown`.
- A change of usermod settings or of segment zero discards pending replies, turns the spot lamps off and polls again.

## Response

The response is Protocol Buffers (`contracts/device/render.proto` in the Kustgloed project). The generated code and the nanopb 0.4.9.1 decoder live in `nanopb/`.

- The response head is limited to 768 bytes and needs `Content-Length`. Chunked transfer is rejected. The body is limited to 4096 bytes.
- ESP32-C3 and ESP32-S3 decode the body from the connection. ESP8266 and classic ESP32 collect the reply in a bounded buffer first.
- Limits are 26 tracks, 256 segments and 51 lamps on pixels 1 to 51. Segment durations are 1 to 60000 ms, and values are 0 to 100 percent.
- The whole response is checked before activation. A malformed, over-limit or invalid response keeps the previous state and counts as rejected (code 6).
- A response without a definition is valid only when its digest equals the active one.
- The player writes updates to inactive buffers. A complete update takes effect at once, and the shared animation clock keeps running.

## Brightness and fallback

- Spot lamps use the server brightness, which is 0 while the map is off for the night, and otherwise one lamp level set on the server for all maps. WLED brightness is held at full while the usermod drives the strip, so it does not change the lamps. The WLED power limit still applies.
- A channel value is the lamp color scaled by the brightness and the track percentage.
- After the hold lapses without a success, and after a restart, the spot lamps are off.
- A failed poll shows its error code at once. The lamps keep the last complete result until the hold lapses.
- Disabling the usermod releases the LEDs. A realtime override suspends the usermod output, including the status lamp.

## System lamp

Physical pixel 0 (the first lamp) shows solid green while the last complete response is fresh and reports usable weather. While the server turns the map off for the night (brightness 0), the status lamp is dark unless a fault blinks. Faults blink red in counted groups: 200 ms on, 200 ms off per blink, then a two-second pause. The status lamp uses a fixed 33% level, independent of the server brightness. It stays available even if segment zero excludes pixel 0.

| Blinks | Meaning |
| --- | --- |
| 1 | Server unreachable, timeout, low heap, or waiting for the first complete response |
| 2 | Wi-Fi/network disconnected |
| 3 | Missing server or key settings, refused server address, unsuitable segment zero, or no worker or buffer |
| 4 | API rejects credentials (HTTP 401/403) |
| 5 | Other non-200 HTTP response (not 400, 401, 403, 404, or 5xx) |
| 6 | Rejected response: bad head, length, protobuf data or values |
| 7 | The last complete response is older than the hold |
| 8 | The server reports that no spot has fresh weather data |
| 9 | HTTP 404: the server or edge refuses the route |
| 10 | HTTP 400: the server rejects the request, for example an unsupported contract or model |
| 11 | HTTP 5xx: server-side failure (for example database unavailable) |
| 12 | TLS verification failed, certificate dates invalid, or system clock unusable for HTTPS |

HTTPS (ESP32-S3 and ESP32-C3, port 443) waits for WLED's own NTP time. Enable NTP in WLED's time settings; without it code 12 persists and no HTTPS poll starts.

The pinned mbedTLS skips certificate dates, so `cert_dates.h` checks them after every handshake, before the first request byte. It checks the leaf and each served intermediate up to the one ISRG Root X1 issued, skips unrelated extras, and rejects a clock past the root's end (2035-06-04). Validity is inclusive at both ends. A failure gives code 12 with the normal backoff and counts one `Kite TLS error`. The clock floor is 2026-01-01 or the build's `KITE_BUILD_EPOCH`, if later. `tools/kite_version.py` takes that epoch from `SOURCE_DATE_EPOCH`, else from the commit time; the release check passes both the commit and its time automatically. The build refuses a source date more than one day ahead of real time; a unit flashed from a build dated ahead shows code 12 until real time catches up (at most a day), then recovers by itself.

Setup and network faults take priority. Otherwise the latest error remains until a successful poll. Code 7 applies when no more specific error exists. Debug logs report changes to the numeric status code without connection settings or credentials.

## Lamp test

`POST /json/state {"kiteLampTest":true}` starts a local test on every pixel except the status lamp. The lamps show orange, red, yellow, green and blue for two seconds each, at 50% level, for 20 seconds. `false` ends the test.

## Diagnostics

`/json/state` exposes a read-only `kiteDisplay` object without settings or credentials.

| Field | Meaning |
| --- | --- |
| `digest` | Active definition digest as 64 hex characters, or `null` |
| `brightness` | Brightness of the shown response, 0 to 255; 0 without one |
| `weather` | `none` (no held response), `ok`, `unavailable` or `simulated` |
| `lastSuccessS` | Seconds since the last successful poll, or `null` |
| `lamps` | Number of lit spot lamps in the held response |
| `rejected` | Rejected responses since boot |

The info panel (`/json/info`, section `u`) reports `Kite wind` (state text and lamp count), `Kite LED capacity` (segment zero without pixel 0), `Kite system blink code`, `Wind data age` (after a success), `Kite lamp test` (seconds left) and `Kite successful polls`. C3 and S3 builds also report `Kite TLS errors`, `Kite worker stack free` (bytes) and `Kite min internal heap` (bytes).

## Native validation

Run from the firmware root. The decoder tests link the vendored C files, built without `-Werror`:

```sh
o="$(mktemp -d)"
for f in pb_decode pb_common render.pb; do cc -std=c99 -Wall -Wextra -c "usermods/kite_wind/nanopb/$f.c" -o "$o/$f.o"; done
for t in status poll-policy cert http access; do
  c++ -std=c++11 -Wall -Wextra -Werror "tools/kite-wind-$t-test.cpp" -o "$o/$t" && "$o/$t" || echo "FAILED $t"
done
c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-proto-test.cpp usermods/kite_wind/render_proto.cpp "$o"/*.o -o "$o/proto" && "$o/proto"
c++ -std=c++11 -Wall -Wextra -Werror tools/kite-wind-render-test.cpp usermods/kite_wind/render_engine.cpp usermods/kite_wind/render_proto.cpp "$o"/*.o -o "$o/render" && "$o/render"
```

The proto test accepts an optional directory of server-written samples (`contracts/device/samples` in the Kustgloed project) as its argument.

The certificate test checks dates, the chain walk, the clock floor, the session gate and the anchor constants. It fails when ISRG Root X1 ends within 18 months of `SOURCE_DATE_EPOCH` or the current time. Also run its clock floor variant:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -DKITE_BUILD_EPOCH=1795000000 -DKITE_TEST_FLOOR=1795000000 tools/kite-wind-cert-test.cpp -o "$o/cert-floor" && "$o/cert-floor"
```
