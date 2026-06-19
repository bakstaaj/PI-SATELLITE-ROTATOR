#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/docker-common.sh
"$DOCKER" build --target native-test -t pi-satellite-rotator-test .
