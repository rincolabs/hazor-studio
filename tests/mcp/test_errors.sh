#!/bin/bash
source "$(dirname "$0")/../lib/common.sh"

echo "=== Error Validation ==="

echo ""
echo "--- Tool inexistente ---"
test_error "nonexistent_tool" '{}' "Unknown tool"

echo ""
echo "--- Parâmetro fora do range ---"
test_error "gaussian_blur" '{"radius":999}' "out of range"

echo ""
echo "--- JSON inválido ---"
RESP=$(curl -s -X POST "${MCP_URL}" \
    -H 'Content-Type: application/json' \
    -d 'not json' 2>/dev/null)
if echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert d.get('success')==False" 2>/dev/null; then
    echo "  ✅ invalid JSON (correctly rejected)"
    PASS=$((PASS+1))
else
    echo "  ❌ invalid JSON — unexpected response"
    FAIL=$((FAIL+1))
    FAILED="$FAILED [invalid_json]"
fi

echo ""
echo "--- Resposta de tool inexistente via GET ---"
RESP=$(curl -s http://localhost:8080/invalid 2>/dev/null)
if echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert 'not found' in d.get('error','').lower()" 2>/dev/null; then
    echo "  ✅ invalid route (correctly rejected)"
    PASS=$((PASS+1))
else
    echo "  ❌ invalid route — unexpected response"
    FAIL=$((FAIL+1))
    FAILED="$FAILED [invalid_route]"
fi

echo ""
echo "--- Blend mode inválido ---"
test_error "set_layer_blend_mode" '{"index":0,"mode":99}' "out of range"

echo ""
echo "--- Crop com x negativo ---"
test_error "crop" '{"x":-5,"y":0,"width":10,"height":10}' "out of range"

report
