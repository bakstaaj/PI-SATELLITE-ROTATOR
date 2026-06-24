#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/docker-common.sh
mkdir -p dist/rpi-zero-32
"$DOCKER" build --target artifact-rpi-zero-32 --output type=local,dest=dist/rpi-zero-32 .
file dist/rpi-zero-32/pi-satellite-rotator
file dist/rpi-zero-32/witmotion-tool
