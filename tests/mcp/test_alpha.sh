#!/bin/bash
source "$(dirname "$0")/../lib/common.sh"

echo "=== Alpha Preservation Tests ==="

test_tool "new_document" '{"width":50,"height":50}' "create doc for alpha test"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill with semi-transparent"

echo ""
echo "--- invert_colors ---"
test_tool "invert_colors" '{}' "invert preserves alpha"

echo ""
echo "--- adjust_brightness ---"
test_tool "new_document" '{"width":50,"height":50}' "fresh doc"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill semi-transparent"
test_tool "adjust_brightness" '{"value":0.5}' "brightness preserves alpha"

echo ""
echo "--- gaussian_blur ---"
test_tool "new_document" '{"width":50,"height":50}' "fresh doc"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill semi-transparent"
test_tool "gaussian_blur" '{"radius":3}' "blur preserves alpha"

echo ""
echo "--- grayscale ---"
test_tool "new_document" '{"width":50,"height":50}' "fresh doc"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill semi-transparent"
test_tool "grayscale" '{}' "grayscale preserves alpha"

echo ""
echo "--- adjust_saturation ---"
test_tool "new_document" '{"width":50,"height":50}' "fresh doc"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill semi-transparent"
test_tool "adjust_saturation" '{"value":0.5}' "saturation preserves alpha"

echo ""
echo "--- adjust_hue ---"
test_tool "new_document" '{"width":50,"height":50}' "fresh doc"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill semi-transparent"
test_tool "adjust_hue" '{"value":0.3}' "hue preserves alpha"

echo ""
echo "--- noise_reduce ---"
test_tool "new_document" '{"width":50,"height":50}' "fresh doc"
test_tool "fill_layer" '{"index":0,"red":100,"green":150,"blue":200,"alpha":200}' "fill semi-transparent"
test_tool "noise_reduce" '{"strength":2}' "noise reduce preserves alpha"

report
