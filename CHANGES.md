<!--
SPDX-License-Identifier: EUPL-1.2
Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
-->

# Changes from upstream

The base is the upstream **development snapshot 17.0.0-devV5**, pinned to
`3cdb5108b924ec26dec81d2c92962b70b185bd01`. It precedes an upstream release.

| Path | Reason |
| --- | --- |
| `usermods/kite_wind/` | Adds the wind map usermod, controller setup notes, and file licence notices. |
| `tools/kite-wind-display-test.cpp` | Checks the weather display parser. |
| `tools/kite-wind-http-test.cpp` | Checks the bounded HTTP exchange. |
| `tools/kite-wind-night-dimming-test.cpp` | Checks the nightly brightness adjustment. |
| `tools/kite-wind-poll-policy-test.cpp` | Checks poll timing and host classification. |
| `tools/kite-wind-render-test.cpp` | Checks the render contract parser and player. |
| `tools/kite-wind-status-test.cpp` | Checks system lamp blink timing. |
| `tools/release-check/` | Builds and scans packaged release images in a clean container for all four supported boards. |
| `tools/partitions-kite-s3.csv` | Supplies the existing ESP32-S3 flash partition layout. |
| `tools/partitions-kite-c3.csv` | Supplies the ESP32-C3 flash partition layout. |
| `platformio_override.sample.ini` | Preserves the upstream examples and adds the four Kustgloed board environments. |
| `wled00/asyncDNS.h` | Holds the lwIP core lock during DNS calls where core locking is enabled. |
| `wled00/cfg.cpp` | Invokes the usermod hooks during protected configuration load and save, and clears the save flag before the write so a change made during a save is not lost. |
| `wled00/fcn_declare.h` | Declares the protected configuration hooks. |
| `wled00/um_manager.cpp` | Dispatches the protected configuration hooks to enabled usermods. |
| `wled00/set.cpp` | Redacts usermod settings values in debug output. |
| `wled00/data/common.js` | Removes the shared development banner. |
| `wled00/data/index.htm` | Replaces the main controls with a settings redirect and fallback link. |
| `wled00/data/index.js` | Removes the live-data overlay tied to the previous page. |
| `wled00/data/settings_um.htm` | Removes the global I²C and SPI controls from the usermod settings page. |
| `wled00/data/welcome.htm` | Removes the welcome page development banner. |
| `wled00/wled_server.cpp` | Redirects the root path to settings when the build flag enables it. |
| `wled00/xml.cpp` | Removes form initialization for the removed I²C and SPI controls. |
| `.github/FUNDING.yml` | Removes the upstream project's funding configuration. |
| `.github/ISSUE_TEMPLATE/bug.yml` | Removes upstream issue triage instructions. |
| `.github/ISSUE_TEMPLATE/config.yml` | Removes upstream issue form routing. |
| `.github/ISSUE_TEMPLATE/feature_request.md` | Removes upstream feature request routing. |
| `.github/workflows/build.yml` | Removes upstream build automation. |
| `.github/workflows/nightly.yml` | Removes upstream nightly automation. |
| `.github/workflows/pr-merge.yaml` | Removes upstream merge automation. |
| `.github/workflows/release.yml` | Removes upstream release automation. |
| `.github/workflows/stale.yml` | Removes upstream stale-issue automation. |
| `.github/workflows/test.yaml` | Removes upstream test automation. |
| `.github/workflows/usermods.yml` | Removes upstream usermod automation. |
| `.github/workflows/wled-ci.yml` | Removes upstream CI automation. |
| `readme.md` → `README.md` | Replaces the upstream introduction with Kustgloed build and source instructions. |
| `NOTICE.md` | Records upstream attribution and the pinned base. |
| `CHANGES.md` | Lists the changes from upstream. |
| `SECURITY.md` | Gives a private vulnerability reporting contact. |
| `docs/THIRD-PARTY.md` | Records third-party terms and open distribution checks. |
| `docs/UPSTREAM.md` | Describes how to update the upstream base. |
