#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/docker-common.sh
mkdir -p dist
"$DOCKER" build --target artifact --output type=local,dest=dist .
file dist/pi-satellite-rotator
file dist/witmotion-tool
