MCP_URL="${MCP_URL:-http://localhost:8080/mcp/execute}"
PASS=0
FAIL=0
FAILED=""

test_tool() {
    local tool="$1"
    local params="$2"
    local desc="${3:-$tool}"
    local r curl_exit

    r=$(curl -s -X POST "$MCP_URL" \
        -H 'Content-Type: application/json' \
        -d "{\"tool\":\"$tool\",\"params\":$params}" 2>/dev/null)
    curl_exit=$?

    if [ $curl_exit -ne 0 ]; then
        echo "  ❌ $desc — connection failed"
        FAIL=$((FAIL+1))
        FAILED="$FAILED [$tool: connection failed]"
        return
    fi

    local success error
    success=$(echo "$r" | python3 -c "
import sys,json
try:
    print(json.load(sys.stdin).get('success', False))
except:
    print('False')
" 2>/dev/null)
    error=$(echo "$r" | python3 -c "
import sys,json
try:
    print(json.load(sys.stdin).get('error', 'null'))
except:
    print('parse_error')
" 2>/dev/null)

    if [ "$success" = "True" ]; then
        echo "  ✅ $desc"
        PASS=$((PASS+1))
    else
        echo "  ❌ $desc — $error"
        FAIL=$((FAIL+1))
        FAILED="$FAILED [$tool: $error]"
    fi
}

test_error() {
    local tool="$1"
    local params="$2"
    local expected_msg="$3"
    local desc="${4:-$tool}"

    local r=$(curl -s -X POST "$MCP_URL" \
        -H 'Content-Type: application/json' \
        -d "{\"tool\":\"$tool\",\"params\":$params}" 2>/dev/null)

    local success error
    success=$(echo "$r" | python3 -c "
import sys,json
try:
    print(json.load(sys.stdin).get('success', False))
except:
    print('False')
" 2>/dev/null)
    error=$(echo "$r" | python3 -c "
import sys,json
try:
    print(json.load(sys.stdin).get('error', ''))
except:
    print('parse_error')
" 2>/dev/null)

    if [ "$success" = "False" ] && echo "$error" | grep -q "$expected_msg"; then
        echo "  ✅ $desc (correctly rejected: $error)"
        PASS=$((PASS+1))
    else
        echo "  ❌ $desc — expected error '$expected_msg', got success=$success error='$error'"
        FAIL=$((FAIL+1))
        FAILED="$FAILED [$tool: expected '$expected_msg' got '$error']"
    fi
}

report() {
    echo ""
    echo "=========================================="
    echo "RESULTS: $PASS passed, $FAIL failed"
    if [ $FAIL -gt 0 ]; then
        echo "FAILED:$FAILED"
    fi
    echo "=========================================="
    return $FAIL
}
