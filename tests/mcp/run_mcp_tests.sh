#!/bin/bash
# MCP Integration Test Runner
# Usage: run_mcp_tests.sh [--app PATH]
#   Assumes the app is already running on port 8080, or starts it with --app

APP_PATH=""
APP_ARGS=""
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

while [ $# -gt 0 ]; do
    case "$1" in
        --app) APP_PATH="$2"; shift 2 ;;
        --app-args) APP_ARGS="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

cleanup() {
    if [ -n "$APP_PID" ]; then
        kill "$APP_PID" 2>/dev/null || true
    fi
}

if [ -n "$APP_PATH" ]; then
    echo "Starting app: $APP_PATH"
    "$APP_PATH" $APP_ARGS &
    APP_PID=$!
    echo "App PID: $APP_PID"
    sleep 3

    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "ERROR: App failed to start"
        exit 1
    fi

    trap cleanup EXIT
fi

# Wait for MCP server
echo "Waiting for MCP server..."
for i in $(seq 1 10); do
    if curl -s http://localhost:8080/mcp/tools > /dev/null 2>&1; then
        echo "MCP server ready"
        break
    fi
    if [ "$i" -eq 10 ]; then
        echo "ERROR: MCP server not responding"
        exit 1
    fi
    sleep 1
done

echo ""
echo "=========================================="
echo " MCP Integration Test Suite"
echo "=========================================="
echo ""

OVERALL_PASS=0
OVERALL_FAIL=0

run_suite() {
    local name="$1"
    local script="$2"
    echo ""
    echo "--- Suite: $name ---"

    local output
    output=$(bash "$script" 2>&1)
    echo "$output"

    local last_line
    last_line=$(echo "$output" | grep "^RESULTS:" | tail -1)
    local passed=$(echo "$last_line" | sed -n 's/.* \([0-9]\+\) passed.*/\1/p' || echo "0")
    local failed=$(echo "$last_line" | sed -n 's/.* \([0-9]\+\) failed.*/\1/p' || echo "0")

    OVERALL_PASS=$((OVERALL_PASS + passed))
    OVERALL_FAIL=$((OVERALL_FAIL + failed))
}

run_suite "All Tools"      "$SCRIPT_DIR/test_all_tools.sh"
run_suite "Errors"         "$SCRIPT_DIR/test_errors.sh"
run_suite "Alpha"          "$SCRIPT_DIR/test_alpha.sh"

echo ""
echo "=========================================="
echo " OVERALL: $OVERALL_PASS passed, $OVERALL_FAIL failed"
echo "=========================================="

exit $OVERALL_FAIL
