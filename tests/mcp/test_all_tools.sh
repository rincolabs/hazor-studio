#!/bin/bash
source "$(dirname "$0")/../lib/common.sh"

echo "=== Document ==="
test_tool "new_document" '{"width":800,"height":600}'
test_tool "resize_document" '{"width":1024,"height":768}'

echo ""
echo "=== Layer ==="
test_tool "add_layer" '{}'
test_tool "set_layer_opacity" '{"index":0,"opacity":0.5}'
test_tool "set_layer_visibility" '{"index":1,"visible":0}'
test_tool "set_layer_visibility" '{"index":1,"visible":1}'
test_tool "fill_layer" '{"index":0,"red":255,"green":0,"blue":0,"alpha":255}'
test_tool "set_layer_blend_mode" '{"index":0,"mode":1}'
test_tool "set_layer_blend_mode" '{"index":0,"mode":10}'
test_tool "set_layer_blend_mode" '{"index":0,"mode":0}'
test_tool "duplicate_layer" '{"index":0}'
test_tool "remove_layer" '{"index":1}'
test_tool "add_layer" '{}'
test_tool "merge_visible" '{}'

echo ""
echo "=== Adjust ==="
test_tool "adjust_brightness" '{"value":0.3}'
test_tool "adjust_contrast" '{"value":0.2}'
test_tool "adjust_saturation" '{"value":0.3}'
test_tool "adjust_hue" '{"value":0.2}'
test_tool "auto_contrast" '{}'

echo ""
echo "=== Filter ==="
test_tool "gaussian_blur" '{"radius":3}'
test_tool "sharpen" '{"value":1}'
test_tool "median_blur" '{"ksize":5}'
test_tool "edge_detect" '{"threshold1":50,"threshold2":150}'
test_tool "grayscale" '{}'
test_tool "invert_colors" '{}'
test_tool "noise_reduce" '{"strength":2}'
test_tool "posterize" '{"levels":4}'
test_tool "threshold" '{"value":128}'
test_tool "remove_background" '{}'

echo ""
echo "=== Transform ==="
test_tool "flip_horizontal" '{}'
test_tool "flip_vertical" '{}'
test_tool "rotate" '{"angle":90}'
test_tool "resize_layer" '{"width":400,"height":300}'
test_tool "crop" '{"x":10,"y":10,"width":200,"height":150}'

echo ""
echo "=== Viewport ==="
test_tool "zoom" '{"level":2}'
test_tool "reset_view" '{}'

report
