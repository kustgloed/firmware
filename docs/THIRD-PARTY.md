<!--
SPDX-License-Identifier: EUPL-1.2
Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
-->

# Third-party notices

The kite usermod declares no direct PlatformIO libraries. It uses code
vendored by the upstream firmware and libraries provided by the selected
board frameworks. See the pinned [PlatformIO configuration](../platformio.ini)
and the existing notices under [vendored dependencies](../wled00/src/dependencies/).

| Component | Licence or provenance |
| --- | --- |
| Upstream firmware | [EUPL v1.2 or later](../LICENSE). |
| ArduinoJson 6.18.1 | MIT, as recorded in the [vendored header](../wled00/src/dependencies/json/ArduinoJson-v6.h). |
| Timezone | CC BY-SA 3.0, as recorded in the [vendored licence](../wled00/src/dependencies/timezone/LICENSE.md). |
| NeoPixelBus | LGPL-3.0. See the upstream licence for the [standard pin](https://github.com/Makuna/NeoPixelBus/blob/1d7ff38f14d04a976f9c6e365c83a232bcba04fc/COPYING) and the [ESP32 framework pin](https://github.com/Makuna/NeoPixelBus/blob/76afe832f74b0738a3fa1bba0caf389ade9e7693/COPYING). |
| ESP8266 Arduino core | [LGPL-2.1 and component-specific terms](https://github.com/esp8266/Arduino/blob/3.1.2/LICENSE). |
| Arduino-ESP32 | [LGPL-2.1 and component-specific terms](https://github.com/espressif/arduino-esp32/blob/master/LICENSE.md). |
| lwIP | [BSD-style licence](https://github.com/lwip-tcpip/lwip/blob/master/COPYING). |
| Mbed TLS | [Apache-2.0 or GPL-2.0-or-later](https://github.com/Mbed-TLS/mbedtls/blob/development/LICENSE). |
| ISRG Root X1 | Public trust anchor from [Let's Encrypt](https://letsencrypt.org/certificates/). |

## Open checks before device binaries are distributed

- **Timezone (CC BY-SA 3.0)** — verify the provenance of the vendored
  revision and compatibility of its terms for binary distribution.
- **NeoPixelBus (LGPL-3.0)** — confirm the exact version linked in each
  shipped build and satisfy the applicable notice, source, and relinking
  requirements.

These checks apply to device binary distribution. The source can be made
public with its component notices.
