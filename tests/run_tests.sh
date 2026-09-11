#!/bin/sh
# SPDX-License-Identifier: MIT
# Build and run tuituirss tests.
#
#   ./tests/run_tests.sh              # unit tests only
#   RUN_INTEGRATION=1 ./tests/run_tests.sh
#
# Integration tests need TTRSS_URL, TTRSS_USER and TTRSS_PASS (or
# TTUIRSS_PASSWORD) and are skipped when those are not set.

set -e

cd "$(dirname "$0")/.."

echo "== building =="
make

echo
echo "== unit tests =="
make test

if [ "${RUN_INTEGRATION:-0}" = "1" ]; then
    echo
    echo "== integration tests =="
    if [ -z "$TTRSS_URL" ] || [ -z "$TTRSS_USER" ]; then
        echo "skipped: TTRSS_URL / TTRSS_USER not set"
    else
        make integration
    fi
else
    echo
    echo "(set RUN_INTEGRATION=1 to run integration tests)"
fi
