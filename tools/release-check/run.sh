#!/usr/bin/env bash
# SPDX-License-Identifier: EUPL-1.2
# Copyright (c) 2026 Chura IT Consultancy (Kustgloed)

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 EMPTY_OUTPUT_DIRECTORY [REVISION]" >&2
    exit 2
fi

tools_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$tools_dir/../.." && pwd)"
revision="$(git -C "$repo" rev-parse --verify --end-of-options "${2:-HEAD}^{commit}")"
mkdir -p -- "$1"
out="$(cd "$1" && pwd)"
if [[ -n "$(ls -A -- "$out")" ]]; then
    echo "The output directory must be empty." >&2
    exit 2
fi

scratch="$(mktemp -d)"
trap 'rm -f -- "$scratch/source.tar" "$scratch/image-id"; rmdir -- "$scratch"' EXIT
git -C "$repo" archive --format=tar --output="$scratch/source.tar" "$revision"
chmod a+r "$scratch/source.tar"

docker build --platform linux/arm64 --iidfile "$scratch/image-id" "$tools_dir"
docker run --rm --platform linux/arm64 --user "$(id -u):$(id -g)" \
    --mount "type=bind,source=$scratch/source.tar,target=/input/source.tar,readonly" \
    --mount "type=bind,source=$out,target=/out" \
    "$(cat "$scratch/image-id")" "$revision"
