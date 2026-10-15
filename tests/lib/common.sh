MCP_URL="${MCP_URL:-http://localhost:8085/mcp}"
MCP_PROTOCOL_VERSION="${MCP_PROTOCOL_VERSION:-2026-07-28}"
MCP_REQUEST_ID=0
PASS=0
FAIL=0
FAILED=""

mcp_tool_call() {
    local tool="$1"
    local params="$2"

    MCP_REQUEST_ID=$((MCP_REQUEST_ID + 1))
    MCP_RESPONSE=$(curl -sS --max-time 10 -X POST "$MCP_URL" \
        -H 'Content-Type: application/json' \
        -H "MCP-Protocol-Version: $MCP_PROTOCOL_VERSION" \
        -H 'Mcp-Method: tools/call' \
        -H "Mcp-Name: $tool" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$MCP_REQUEST_ID,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$params,\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"$MCP_PROTOCOL_VERSION\",\"io.modelcontextprotocol/clientCapabilities\":{}}}}" 2>&1)
    MCP_CURL_EXIT=$?
}

mcp_response_success() {
    echo "$MCP_RESPONSE" | python3 -c '
import json
import sys

try:
    response = json.load(sys.stdin)
    result = response.get("result", {})
    success = result.get("structuredContent", {}).get("success") is True
    print("True" if success and result.get("isError") is False else "False")
except Exception:
    print("False")
' 2>/dev/null
}

mcp_response_error() {
    echo "$MCP_RESPONSE" | python3 -c '
import json
import sys

try:
    response = json.load(sys.stdin)
    if "error" in response:
        print(response["error"].get("message", "null"))
    else:
        print(response.get("result", {}).get("structuredContent", {}).get("message", "null"))
except Exception:
    print("parse_error")
' 2>/dev/null
}

mcp_response_is_error() {
    echo "$MCP_RESPONSE" | python3 -c '
import json
import sys

try:
    response = json.load(sys.stdin)
    result = response.get("result", {})
    is_error = "error" in response or result.get("isError") is True
    print("True" if is_error else "False")
except Exception:
    print("False")
' 2>/dev/null
}

test_tool() {
    local tool="$1"
    local params="$2"
    local desc="${3:-$tool}"
    local success error

    mcp_tool_call "$tool" "$params"

    if [ "$MCP_CURL_EXIT" -ne 0 ]; then
        echo "  ❌ $desc — connection failed"
        FAIL=$((FAIL+1))
        FAILED="$FAILED [$tool: connection failed]"
        return
    fi

    success=$(mcp_response_success)
    error=$(mcp_response_error)

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

    local is_error error
    mcp_tool_call "$tool" "$params"

    if [ "$MCP_CURL_EXIT" -ne 0 ]; then
        echo "  ❌ $desc — connection failed"
        FAIL=$((FAIL+1))
        FAILED="$FAILED [$tool: connection failed]"
        return
    fi

    is_error=$(mcp_response_is_error)
    error=$(mcp_response_error)

    if [ "$is_error" = "True" ] && echo "$error" | grep -q "$expected_msg"; then
        echo "  ✅ $desc (correctly rejected: $error)"
        PASS=$((PASS+1))
    else
        echo "  ❌ $desc — expected error '$expected_msg', got isError=$is_error error='$error'"
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
