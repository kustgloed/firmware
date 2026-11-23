# SPDX-License-Identifier: EUPL-1.2
# Copyright (c) 2026 Chura IT Consultancy (Kustgloed)

import configparser
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import venv

SOURCE = Path("/work/firmware")
OUTPUT = Path("/out")
VENV = Path("/work/venv")
CORE = Path("/opt/platformio")
PROFILES = {
    "kite": "ESP8266",
    "kite_s3": "ESP32-S3_16MB_opi",
    "kite_c3": "ESP32-C3",
    "esp32dev": "ESP32",
}
PREFIX_FLAGS = [
    f"-{kind}-prefix-map={source}={target}"
    for source, target in ((SOURCE, "/src/kustgloed"), (CORE, "/sdk/platformio"))
    for kind in ("ffile", "fmacro")
]
LOCAL_PATH = re.compile(rb"/(?:Users|home)(?=/|[\x00\t\r\n ]|$)")
COMPILER = re.compile(r"^\s*(?:\S*/)?(?:(?:xtensa|riscv32)[-\w]*-)?(?:gcc|g\+\+)\s")
SOURCE_FILE = re.compile(r"\.(?:c|cc|cpp|cxx)(?:\s|$)")


def logged(log_name, *args):
    log = OUTPUT / f"{log_name}.log"
    with log.open("w") as stream:
        result = subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{log.name} failed with exit status {result.returncode}")


def read_config(path):
    config = configparser.ConfigParser(
        interpolation=None, strict=False, inline_comment_prefixes=("#", ";")
    )
    with Path(path).open() as stream:
        config.read_file(stream)
    return config


def prepare_source():
    SOURCE.mkdir()
    with tarfile.open("/input/source.tar") as archive:
        archive.extractall(SOURCE, filter="data")
    os.chdir(SOURCE)
    for path in ("platformio_override.ini", "wled00/my_config.h", "wled00/html_ui.h"):
        if Path(path).exists():
            raise RuntimeError(f"unexpected build input in source archive: {path}")
    # Supply the canonical public identity for upstream repository metadata.
    subprocess.run(("git", "init", "-q"), check=True)
    subprocess.run(
        ("git", "remote", "add", "origin", "https://github.com/kustgloed/firmware.git"),
        check=True,
    )
    upstream = read_config("platformio.ini")
    override = read_config("platformio_override.sample.ini")
    base_flags = {
        "kite": override["env:kite"]["build_flags"],
        "kite_s3": "${env:esp32s3dev_16MB_opi.build_flags}",
        "kite_c3": override["env:kite_c3"]["build_flags"],
        "esp32dev": upstream["env:esp32dev"]["build_flags"],
    }
    for profile, flags in base_flags.items():
        override[f"env:{profile}"]["build_flags"] = "\n".join([flags, *PREFIX_FLAGS])
    with Path("platformio_override.ini").open("w") as stream:
        override.write(stream)


def verify_compiler_flags(profile):
    mapped = total = 0
    with (OUTPUT / f"{profile}.log").open(errors="replace") as stream:
        for line in stream:
            if COMPILER.match(line) and " -c " in line and SOURCE_FILE.search(line):
                total += 1
                mapped += all(flag in line for flag in PREFIX_FLAGS)
    if not total or mapped != total:
        raise RuntimeError(f"{profile}: only {mapped}/{total} C/C++ commands used all path maps")
    return total


def check_image(image, primary):
    raw = image.read_bytes()
    payload = gzip.decompress(raw) if image.suffix == ".gz" else raw
    if not payload:
        raise RuntimeError(f"{image.name}: empty firmware image")
    if image.suffix == ".gz" and payload != primary.read_bytes():
        raise RuntimeError(f"{image.name}: gzip payload differs from the raw firmware")
    hits = len(LOCAL_PATH.findall(payload))
    if image.suffix == ".gz":
        hits += len(LOCAL_PATH.findall(raw))
    digest = hashlib.sha256(raw).hexdigest()
    return len(raw), digest, hits


def source_identity():
    commit = os.environ.get("KITE_FW_COMMIT", "")
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "")
    if not re.fullmatch(r"[0-9a-f]{7,40}", commit) or not sys.argv[1].startswith(commit):
        raise RuntimeError("KITE_FW_COMMIT must name the source commit (run.sh sets it)")
    if not re.fullmatch(r"[0-9]{1,10}", epoch):
        raise RuntimeError("SOURCE_DATE_EPOCH must be the commit time (run.sh sets it)")
    return commit, epoch


def build_and_scan(report):
    commit, epoch = source_identity()
    print(f"Source date epoch {epoch}", file=report)
    prepare_source()
    Path.home().mkdir(parents=True, exist_ok=True)
    venv.create(VENV, with_pip=True)
    os.environ["PATH"] = f"{VENV / 'bin'}:{os.environ['PATH']}"
    logged("pip", "python", "-m", "pip", "install", "-r", "requirements.txt")
    logged("npm", "npm", "ci", "--no-audit", "--no-fund")
    logged("ui-build", "npm", "run", "build")
    logged("ui-tests", "npm", "test")
    # Fails when the source date is within 18 months of the trust anchor end.
    cert_test = Path("/work/kite-wind-cert-test")
    logged("cert-test-build", "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
           "tools/kite-wind-cert-test.cpp", "-o", str(cert_test))
    logged("cert-test", str(cert_test))
    if not Path("wled00/html_ui.h").is_file() or Path("wled00/my_config.h").exists():
        raise RuntimeError("UI generation did not complete with clean configuration")

    version = json.loads(Path("package.json").read_text())["version"]
    artifacts = {}
    for profile, release_name in PROFILES.items():
        print(f"Building {profile}", flush=True)
        logged(profile, "pio", "run", "-v", "-j", "4", "-e", profile)
        total = verify_compiler_flags(profile)
        if profile == "kite_c3" and f"+{commit}.kite_c3, build epoch {epoch}" not in (OUTPUT / "kite_c3.log").read_text(errors="replace"):
            raise RuntimeError("kite_c3: firmware version or build epoch missing from the build log")
        print(f"{profile}: {total} C/C++ commands contain all four map arguments", file=report)
        primary = Path("build_output/release") / f"WLED_{version}_{release_name}.bin"
        if not primary.is_file():
            raise RuntimeError(f"{profile}: missing release image")
        packaged = sorted(primary.parent.glob(f"{primary.name}*"))
        factory = Path(".pio/build") / profile / "firmware.factory.bin"
        if factory.is_file():
            # Preserve it before another environment can clean .pio/build.
            saved = Path("/work") / f"{profile}-factory.bin"
            shutil.copyfile(factory, saved)
            packaged.append(saved)
        if profile == "kite" and primary.with_suffix(".bin.gz") not in packaged:
            raise RuntimeError("kite: missing gzip release image")
        if profile == "kite_s3" and not factory.is_file():
            raise RuntimeError("kite_s3: missing factory image")
        artifacts[profile] = (primary, packaged)

    unsafe = False
    for profile, (primary, packaged) in artifacts.items():
        for image in packaged:
            size, digest, hits = check_image(image, primary)
            print(f"{profile}/{image.name}: {size} bytes sha256={digest} path_matches={hits}", file=report)
            unsafe |= hits != 0
    if unsafe:
        raise RuntimeError("packaged images contain /Users or /home paths")

    for profile, (_, packaged) in artifacts.items():
        target = OUTPUT / "images" / profile
        target.mkdir(parents=True)
        for image in packaged:
            shutil.copyfile(image, target / image.name)


if __name__ == "__main__":
    with (OUTPUT / "results.txt").open("w", buffering=1) as report:
        print(f"Source commit {sys.argv[1]}", file=report)
        try:
            build_and_scan(report)
        except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as error:
            print(f"FAIL {error}", file=report)
            sys.exit(f"Release check failed. See results.txt and build logs. {error}")
        print("PASS all profiles built and all packaged images passed the path scan", file=report)
    print((OUTPUT / "results.txt").read_text(), end="")
