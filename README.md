<!--
SPDX-License-Identifier: EUPL-1.2
Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
-->

# Kustgloed firmware

Firmware for the Kustgloed wind map, based on WLED. It polls the wind map
service and displays conditions on addressable LEDs.

| PlatformIO environment | Target |
| --- | --- |
| `kite_s3` | ESP32-S3 with 16 MB flash and octal PSRAM |
| `kite_c3` | ESP32-C3, 4 MB flash |
| `kite` | ESP8266 D1 mini |
| `esp32dev` | Classic ESP32 development board |

## Build from source

Install Git, Node.js and npm, and PlatformIO Core. Install Python tools
in a virtual environment. From a clean clone of this repository, run

```sh
cp platformio_override.sample.ini platformio_override.ini
npm ci
npm run build
pio run -e kite_s3
```

Choose `kite_c3`, `kite`, or `esp32dev` for the corresponding board. The UI build
generates headers from the checked-in UI source. The build uses the
public sample and upstream defaults. Configure device Wi-Fi and service
access after flashing.

## ESP32-C3 build

`kite_c3` polls `GET /device/render` over verified HTTPS, one request per poll,
and decodes the protobuf response from the connection. It uses its own
partition table, `tools/partitions-kite-c3.csv`, with two OTA slots of 1.875 MiB and a
192 KiB filesystem. The table cannot change over OTA. Existing C3
controllers need one USB reflash, which wipes presets and `cfg.json`.
Judge the image size by the real `firmware.bin` size against the
1,966,080-byte slot, not by the PlatformIO "Flash used" figure.

## Release build check

From the firmware checkout, use Git and Docker to build into an empty output directory.

```sh
output="$(mktemp -d)"
printf '%s\n' "$output"
tools/release-check/run.sh "$output"
```

The check builds `kite`, `kite_s3`, `kite_c3`, and `esp32dev` in a clean Linux ARM64
container. It builds the source at `HEAD` by default. Pass a commit or tag
as the second argument to select another revision. The source archive excludes
working-tree files and local device settings.

The check verifies both compiler path-map flags and scans the raw, gzip,
and factory images produced by the build for `/Users` and `/home` paths.
A passing run writes the verified images under `images/` in the output
directory, with build logs and SHA-256 checksums in `results.txt`.

## Flash

Connect the confirmed board by USB and close any serial monitor using that
port. Find the port with `pio device list`, then upload the image for your
board. For example, run `pio run -e kite_s3 -t upload --upload-port <port>`
with the actual port in place of `<port>`. See the
[usermod setup guide](usermods/kite_wind/readme.md) for controller settings.

## Source and licence

The firmware is distributed under the [EUPL v1.2](LICENSE). This repository
carries the complete firmware source, with a tag for each device firmware
release. Use the tag corresponding to the version supplied with your device.
See the [upstream notice](NOTICE.md), [change list](CHANGES.md), and
[third-party notices](docs/THIRD-PARTY.md) for component terms.

This project is independent and has no affiliation with the WLED project.

Report vulnerabilities using the [security policy](SECURITY.md).
