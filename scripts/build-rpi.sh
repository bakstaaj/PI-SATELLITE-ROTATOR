#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./scripts/build-rpi64.sh
cp -f dist/rpi64/pi-satellite-rotator dist/pi-satellite-rotator
cp -f dist/rpi64/witmotion-tool dist/witmotion-tool
file dist/pi-satellite-rotator
file dist/witmotion-tool
