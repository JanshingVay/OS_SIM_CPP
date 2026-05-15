#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec bash ./01_RUN_ALL_TESTS_LINUX.sh "$@"
