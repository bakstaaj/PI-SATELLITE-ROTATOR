#!/usr/bin/env bash
set -euo pipefail

DOCKER_DESKTOP_BIN='/c/Program Files/Docker/Docker/resources/bin'
if [[ -d "$DOCKER_DESKTOP_BIN" ]]; then
    export PATH="$DOCKER_DESKTOP_BIN:$PATH"
fi

if command -v docker >/dev/null 2>&1; then
    DOCKER=docker
elif [[ -x "$DOCKER_DESKTOP_BIN/docker.exe" ]]; then
    DOCKER="$DOCKER_DESKTOP_BIN/docker.exe"
else
    echo 'Docker CLI was not found. Start Docker Desktop and add docker.exe to PATH.' >&2
    exit 1
fi
