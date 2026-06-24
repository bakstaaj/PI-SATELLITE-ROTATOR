#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/docker-common.sh
mkdir -p dist/rpi64
"$DOCKER" build --target artifact-rpi64 --output type=local,dest=dist/rpi64 .
file dist/rpi64/pi-satellite-rotator
file dist/rpi64/witmotion-tool
