#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/module_tests"
bash ./run_module_tests_linux.sh
