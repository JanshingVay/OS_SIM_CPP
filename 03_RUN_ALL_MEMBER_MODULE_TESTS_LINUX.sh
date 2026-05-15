#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/pre_integration"
bash ./run_all_member_module_tests_linux.sh
