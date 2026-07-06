cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# Boost.Test header-only — just need include path
find_package(Boost REQUIRED)

set(TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)
set(TEST_LIBS_COMMON Qt6::Core Qt6::Gui ${OpenCV_LIBS})

# Core document/storage sources needed by tests using ImageController or Commands
set(TEST_TILE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/TileManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TileManager.hpp
    ${CMAKE_SOURCE_DIR}/src/core/DirtyRegion.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DirtyRegion.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/GuideManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/GuideManager.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Tile.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RenderScheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RenderScheduler.hpp
    ${CMAKE_SOURCE_DIR}/src/renderer/DocumentCompositor.cpp
    ${CMAKE_SOURCE_DIR}/src/renderer/DocumentCompositor.hpp
    ${CMAKE_SOURCE_DIR}/src/renderer/RenderContext.hpp
)

set(TEST_ASYNC_SOURCES
    ${CMAKE_SOURCE_DIR}/src/async/AsyncJobSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/async/AsyncJobSystem.hpp
    ${CMAKE_SOURCE_DIR}/src/async/AsyncJob.hpp
)

set(TEST_MEMORY_SOURCES
    ${CMAKE_SOURCE_DIR}/src/memory/BufferPool.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/BufferPool.hpp
)

set(TEST_PROCESSING_SOURCES
    ${CMAKE_SOURCE_DIR}/src/processing/FilterProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/processing/FilterProcessor.hpp
    ${CMAKE_SOURCE_DIR}/src/processing/PreviewRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/processing/PreviewRenderer.hpp
    ${TEST_MEMORY_SOURCES}
)

# ── Theme tests ────────────────────────────────────────────────
add_executable(test-theme
    unit/test_theme.cpp
)
target_include_directories(test-theme PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-theme PRIVATE theme_lib Qt6::Core Qt6::Gui)
add_test(NAME unit-theme COMMAND test-theme)

# ── UI: ColorSwapWidget tests ──────────────────────────────────
add_executable(test-ui-colorswap
    ui/test_swapcolorwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ColorSwapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ColorSwapWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/core/ColorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ColorEngine.hpp
)
target_include_directories(test-ui-colorswap PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-colorswap PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-colorswap COMMAND test-ui-colorswap)
set_tests_properties(ui-colorswap PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── UI: ColorPickerDialog tests ─────────────────────────────
add_executable(test-ui-colorpicker
    ui/test_colorpickerdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/ColorSwapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ColorSwapWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/core/ColorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ColorEngine.hpp
)
target_include_directories(test-ui-colorpicker PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-colorpicker PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-colorpicker COMMAND test-ui-colorpicker)
set_tests_properties(ui-colorpicker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── ImageEngine tests ────────────────────────────────────────
add_executable(test-imageengine
    unit/test_imageengine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
)
target_include_directories(test-imageengine PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-imageengine PRIVATE ${TEST_LIBS_COMMON})
# Disable automatic Boost linkage — header-only mode
add_test(NAME unit-imageengine COMMAND test-imageengine)

# ── ColorSamplerService tests ────────────────────────────────
add_executable(test-colorsamplerservice
    unit/test_colorsamplerservice.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ColorSamplerService.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ColorSamplerService.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerTreeNode.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
)
target_include_directories(test-colorsamplerservice PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-colorsamplerservice PRIVATE Qt6::Core Qt6::Gui ${OpenCV_LIBS})
add_test(NAME unit-colorsamplerservice COMMAND test-colorsamplerservice)

# ── TransformController tests ────────────────────────────────
add_executable(test-transformcontroller
    unit/test_transformcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformController.cpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformController.hpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformTypes.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.hpp
)
target_include_directories(test-transformcontroller PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-transformcontroller PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME unit-transformcontroller COMMAND test-transformcontroller)

# ── BrushPresetManager tests ─────────────────────────────────
add_executable(test-brushpresetmanager
    unit/test_brushpresetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/BrushPresetManager.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/BrushPresetManager.hpp
)
target_include_directories(test-brushpresetmanager PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-brushpresetmanager PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME unit-brushpresetmanager COMMAND test-brushpresetmanager)

# ── UI: BrushPresetPicker tests ─────────────────────────────
add_executable(test-ui-brushpresetpicker
    ui/test_brushpresetpicker.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BrushPresetPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BrushPresetPicker.hpp
    ${CMAKE_SOURCE_DIR}/src/brush/BrushPresetManager.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/BrushPresetManager.hpp
)
target_include_directories(test-ui-brushpresetpicker PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-brushpresetpicker PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-brushpresetpicker COMMAND test-ui-brushpresetpicker)
set_tests_properties(ui-brushpresetpicker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── ToolCatalog tests ─────────────────────────────────────────
add_executable(test-toolcatalog
    unit/test_toolcatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCatalog.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCall.hpp
)
target_include_directories(test-toolcatalog PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-toolcatalog PRIVATE Qt6::Core)
add_test(NAME unit-toolcatalog COMMAND test-toolcatalog)

# ── TextEditorController tests ────────────────────────────────
add_executable(test-texteditor
    unit/test_texteditor.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextEditorController.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextEditorController.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-texteditor PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-texteditor PRIVATE theme_lib Qt6::Core Qt6::Gui)
add_test(NAME unit-texteditor COMMAND test-texteditor)

# ── SelectionMask tests ──────────────────────────────────────
add_executable(test-selectionmask
    unit/test_selectionmask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
)
target_include_directories(test-selectionmask PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-selectionmask PRIVATE ${TEST_LIBS_COMMON})
add_test(NAME unit-selectionmask COMMAND test-selectionmask)

# ── HttpParser tests ──────────────────────────────────────────
add_executable(test-httpparser
    unit/test_httpparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/HttpParser.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/HttpParser.hpp
    ${CMAKE_SOURCE_DIR}/src/mcp/McpTypes.hpp
)
target_include_directories(test-httpparser PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-httpparser PRIVATE Qt6::Core Qt6::Network)
add_test(NAME unit-httpparser COMMAND test-httpparser)

# ── UI: ToolsPanel test ───────────────────────────────────────
add_executable(test-ui-toolspanel
    ui/test_toolspanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ToolsPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ToolsPanel.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/ColorSwapWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ColorSwapWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/core/ColorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ColorEngine.hpp
)
target_include_directories(test-ui-toolspanel PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-toolspanel PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-toolspanel COMMAND test-ui-toolspanel)
set_tests_properties(ui-toolspanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── Clipboard tests ──────────────────────────────────────────
add_executable(test-clipboard
    unit/test_clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
)
target_include_directories(test-clipboard PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-clipboard PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME unit-clipboard COMMAND test-clipboard)

# ── Copy/Paste integration tests ──────────────────────────────
add_executable(test-copypaste
    unit/test_copypaste.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-copypaste PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-copypaste PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-copypaste COMMAND test-copypaste)

# ── Import Image tests ─────────────────────────────────────────
add_executable(test-importimage
    unit/test_importimage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-importimage PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-importimage PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-importimage COMMAND test-importimage)

# ── Layer Menu tests ──────────────────────────────────────────
add_executable(test-layermenu
    unit/test_layermenu.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-layermenu PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-layermenu PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-layermenu COMMAND test-layermenu)

# ── Multi-select layers tests ─────────────────────────────────
add_executable(test-multiselect
    unit/test_multiselect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-multiselect PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-multiselect PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-multiselect COMMAND test-multiselect)

# ── Image Filter integration tests (param mapping + undo) ─────
add_executable(test-imagefilters
    unit/test_imagefilters.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-imagefilters PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-imagefilters PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-imagefilters COMMAND test-imagefilters)

# ── Gradient feature tests ─────────────────────────────────────
add_executable(test-gradient
    unit/test_gradient.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/io/ImageIO.cpp
    ${CMAKE_SOURCE_DIR}/src/io/ImageIO.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/ImageGenProviderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ImageGenProviderFactory.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/ImageGenProvider.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/A1111ImageGenProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/A1111ImageGenProvider.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/OpenAIImageGenProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/OpenAIImageGenProvider.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/StabilityImageGenProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/StabilityImageGenProvider.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/GeminiImageGenProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/GeminiImageGenProvider.hpp
    ${CMAKE_SOURCE_DIR}/src/ai/AggregatorImageGenProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AggregatorImageGenProvider.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientTypes.hpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientPresetManager.cpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientPresetManager.hpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/gradient/GradientCommand.hpp
)
target_include_directories(test-gradient PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-gradient PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Network ${OpenCV_LIBS}
)
add_test(NAME unit-gradient COMMAND test-gradient)

# ── Groups / accumulatedTransform tests ─────────────────────────
add_executable(test-groups
    unit/test_groups.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-groups PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-groups PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-groups COMMAND test-groups)

# ── Move / Align tests ──────────────────────────────────────────
add_executable(test-move-align
    unit/test_move_align.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformController.cpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformController.hpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformTypes.hpp
)
target_include_directories(test-move-align PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-move-align PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-move-align COMMAND test-move-align)

# ── BrushEngine tests ──────────────────────────────────────────
add_executable(test-brushengine
    unit/test_brushengine.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/DynamicsEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/DynamicsEvaluator.hpp
)
target_include_directories(test-brushengine PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-brushengine PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME unit-brushengine COMMAND test-brushengine)

# ── UI: Dialog tests (param names, preview default, mode) ─────
set(DIALOG_SRCS
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ToolDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ToolDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ParamRow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ParamRow.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/AdjustColorDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/AdjustColorDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/GaussianBlurDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/GaussianBlurDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/SharpenDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/SharpenDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/MedianBlurDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/MedianBlurDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/EdgeDetectDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/EdgeDetectDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/PosterizeDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/PosterizeDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ThresholdDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ThresholdDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/NoiseReduceDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/NoiseReduceDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/RemoveBgDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/RemoveBgDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/FillLayerDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/FillLayerDialog.hpp
)
add_executable(test-ui-dialogs
    ui/test_dialogs.cpp
    ${DIALOG_SRCS}
)
target_include_directories(test-ui-dialogs PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-dialogs PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-dialogs COMMAND test-ui-dialogs)

# ── CommandHistory + Commands tests ────────────────────────────
add_executable(test-commandhistory
    unit/test_commandhistory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerTreeNode.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/Commands.hpp
    ${TEST_TILE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCall.hpp
)
target_include_directories(test-commandhistory PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-commandhistory PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent ${OpenCV_LIBS})
add_test(NAME unit-commandhistory COMMAND test-commandhistory)

# ── History Controller tests ───────────────────────────────────────
add_executable(test-history-controller
    unit/test_history_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerTreeNode.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/Commands.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCall.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/AlphaChannel.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-history-controller PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-history-controller PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-history-controller COMMAND test-history-controller)

# ── History Panel UI tests ─────────────────────────────────────────
add_executable(test-ui-history-panel
    ui/test_history_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/HistoryPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/HistoryPanel.hpp
)
target_include_directories(test-ui-history-panel PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-history-panel PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-history-panel COMMAND test-ui-history-panel)

# ── Data Model tests (Document/LayerTreeNode/Layer) ─────────────
add_executable(test-datamodel
    unit/test_datamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerTreeNode.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextTypes.hpp
)
target_include_directories(test-datamodel PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-datamodel PRIVATE Qt6::Core Qt6::Gui ${OpenCV_LIBS})
add_test(NAME unit-datamodel COMMAND test-datamodel)

# ── Box Selection tests ───────────────────────────────────────
add_executable(test-boxselect
    unit/test_boxselect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/BoxSelection.cpp
    ${CMAKE_SOURCE_DIR}/src/core/BoxSelection.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerTreeNode.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformController.cpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformController.hpp
    ${CMAKE_SOURCE_DIR}/src/transform/TransformTypes.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextTypes.hpp
)
target_include_directories(test-boxselect PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-boxselect PRIVATE Qt6::Core Qt6::Gui ${OpenCV_LIBS})
add_test(NAME unit-boxselect COMMAND test-boxselect)

# ── FilterProcessor tests ──────────────────────────────────────
add_executable(test-filterprocessor
    unit/test_filterprocessor.cpp
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/TileManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TileManager.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Tile.hpp
    ${CMAKE_SOURCE_DIR}/src/core/DirtyRegion.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DirtyRegion.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
)
target_include_directories(test-filterprocessor PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-filterprocessor PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent ${OpenCV_LIBS})
add_test(NAME unit-filterprocessor COMMAND test-filterprocessor)

# ── PerformanceConfig tests ─────────────────────────────────────
add_executable(test-perfconfig
    unit/test_performance_config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PerformanceConfig.hpp
)
target_include_directories(test-perfconfig PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-perfconfig PRIVATE Qt6::Core)
add_test(NAME unit-perfconfig COMMAND test-perfconfig)

# ── Performance benchmarks ───────────────────────────────────────
add_executable(test-benchmarks
    unit/test_benchmarks.cpp
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/TileManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TileManager.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Tile.hpp
    ${CMAKE_SOURCE_DIR}/src/core/DirtyRegion.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DirtyRegion.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
)
target_include_directories(test-benchmarks PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-benchmarks PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent ${OpenCV_LIBS})
add_test(NAME unit-benchmarks COMMAND test-benchmarks)

# ── Controller All Tools tests ──────────────────────────────────
add_executable(test-controller-alltools
    unit/test_controller_alltools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-controller-alltools PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-controller-alltools PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-controller-alltools COMMAND test-controller-alltools)

# ── Edge Cases tests ────────────────────────────────────────────
add_executable(test-edgecases
    unit/test_edgecases.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-edgecases PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-edgecases PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Test Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-edgecases COMMAND test-edgecases)

# ── Layer Mask tests ────────────────────────────────────────────
add_executable(test-layermask
    unit/test_layermask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCatalog.hpp
)
target_include_directories(test-layermask PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-layermask PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-layermask COMMAND test-layermask)

# ── Brush Render tests (offscreen OpenGL) ──────────────────────
add_executable(test-brush-render
    unit/test_brush_render.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/BrushRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/brush/BrushRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.hpp
)
target_include_directories(test-brush-render PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-brush-render PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
)
add_test(NAME unit-brush-render COMMAND test-brush-render)
set_tests_properties(unit-brush-render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── Agent tests (LLMClient + AgentConfig) ───────────────────────
add_executable(test-agent
    unit/test_agent.cpp
    ${CMAKE_SOURCE_DIR}/src/agent/LLMClient.cpp
    ${CMAKE_SOURCE_DIR}/src/agent/LLMClient.hpp
    ${CMAKE_SOURCE_DIR}/src/agent/AgentConfig.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCall.hpp
)
target_include_directories(test-agent PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-agent PRIVATE
    Qt6::Core Qt6::Network
)
add_test(NAME unit-agent COMMAND test-agent)

# ── ToolExecutor tests ──────────────────────────────────────────
add_executable(test-toolexecutor
    unit/test_toolexecutor.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolExecutor.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolExecutor.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCall.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-toolexecutor PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-toolexecutor PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-toolexecutor COMMAND test-toolexecutor)

# ── SelectionMask Extended tests ───────────────────────────────
add_executable(test-selectionmask-ext
    unit/test_selectionmask_extended.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
)
target_include_directories(test-selectionmask-ext PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-selectionmask-ext PRIVATE ${TEST_LIBS_COMMON})
add_test(NAME unit-selectionmask-ext COMMAND test-selectionmask-ext)

# ── MoveTool tests ──────────────────────────────────────────────
add_executable(test-movetool
    unit/test_movetool.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/MoveTool.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/MoveTool.hpp
)
target_include_directories(test-movetool PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-movetool PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME unit-movetool COMMAND test-movetool)

# ── ImageIO tests ───────────────────────────────────────────────
add_executable(test-imageio
    unit/test_imageio.cpp
    ${CMAKE_SOURCE_DIR}/src/io/ImageIO.cpp
    ${CMAKE_SOURCE_DIR}/src/io/ImageIO.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerTreeNode.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Layer.hpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RasterLayerStorage.hpp
    ${CMAKE_SOURCE_DIR}/src/renderer/DocumentCompositor.cpp
    ${CMAKE_SOURCE_DIR}/src/renderer/DocumentCompositor.hpp
    ${CMAKE_SOURCE_DIR}/src/renderer/RenderContext.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
)
target_include_directories(test-imageio PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-imageio PRIVATE Qt6::Core Qt6::Gui ${OpenCV_LIBS})
add_test(NAME unit-imageio COMMAND test-imageio)

# ── UI: SplashScreen tests ─────────────────────────────────────
add_executable(test-ui-splashscreen
    ui/test_splashscreen.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SplashScreen.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SplashScreen.hpp
    ${CMAKE_SOURCE_DIR}/src/resources/resources.qrc
)
target_include_directories(test-ui-splashscreen PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-ui-splashscreen PRIVATE theme_lib Qt6::Test Qt6::Widgets)
add_test(NAME ui-splashscreen
    COMMAND test-ui-splashscreen --platform offscreen
)

# ── Mask Editing Sync tests (integration) ───────────────────────
add_executable(test-mask-sync
    integration/test_mask_editing_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController_Tools.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerEffect.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeVectorRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ShapeRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/controller/ImageController.hpp
    ${TEST_TILE_SOURCES}
    ${TEST_ASYNC_SOURCES}
    ${TEST_PROCESSING_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/CommandHistory.hpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Clipboard.hpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SelectionMask.hpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ImageEngine.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextRenderer.hpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/text/TextLayoutEngine.hpp
)
target_include_directories(test-mask-sync PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-mask-sync PRIVATE
    theme_lib Qt6::Core Qt6::Gui Qt6::Test Qt6::OpenGLWidgets Qt6::Concurrent ${OpenCV_LIBS}
)
add_test(NAME unit-mask-sync COMMAND test-mask-sync)

# ── ShortcutManager tests (Boost.Test) ─────────────────────────────
add_executable(test-shortcutmanager
    unit/test_shortcutmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ShortcutManager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ShortcutManager.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/Theme.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/ThemeManager.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/ThemeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/ClassicTheme.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/ClassicTheme.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/DarkerTheme.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/DarkerTheme.hpp
)
target_include_directories(test-shortcutmanager PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-shortcutmanager PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME unit-shortcutmanager COMMAND test-shortcutmanager)

# ── AppSettings UI tests (QtTest) ─────────────────────────────────
add_executable(test-appsettings
    ui/test_appsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AppSettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AppSettingsDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/GeneralSettingsPage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/GeneralSettingsPage.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/RulerGuideSettingsPage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/RulerGuideSettingsPage.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/ShortcutSettingsPage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ShortcutSettingsPage.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/ShortcutManager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ShortcutManager.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/AgentConfigWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AgentConfigWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/Theme.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/ThemeManager.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/ThemeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/ClassicTheme.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/ClassicTheme.cpp
    ${CMAKE_SOURCE_DIR}/src/theme/DarkerTheme.hpp
    ${CMAKE_SOURCE_DIR}/src/theme/DarkerTheme.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCall.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCatalog.hpp
    ${CMAKE_SOURCE_DIR}/src/tools/ToolCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/agent/AgentConfig.hpp
    ${CMAKE_SOURCE_DIR}/src/agent/AgentPresetManager.hpp
    ${CMAKE_SOURCE_DIR}/src/agent/AgentPresetManager.cpp
    ${CMAKE_SOURCE_DIR}/src/agent/LLMClient.hpp
    ${CMAKE_SOURCE_DIR}/src/agent/LLMClient.cpp
    ${CMAKE_SOURCE_DIR}/src/core/GuideTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/GuideTypes.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPickerDialog.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/SaturationBrightnessArea.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorHueSlider.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorPreviewWidget.hpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/colorpicker/ColorFieldsWidget.hpp
)
target_include_directories(test-appsettings PRIVATE ${TEST_INCLUDES})
target_link_libraries(test-appsettings PRIVATE theme_lib Qt6::Test Qt6::Widgets Qt6::Network)
add_test(NAME ui-appsettings COMMAND test-appsettings)
set_tests_properties(ui-appsettings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── MCP integration tests (shell) ─────────────────────────────
add_test(NAME mcp-tools
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/mcp/run_mcp_tests.sh
        --app "${CMAKE_BINARY_DIR}/Hazor"
        --app-args "--platform;offscreen"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
)
