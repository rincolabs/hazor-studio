cmake_minimum_required(VERSION 3.16)
project(ImageEditor VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    # /utf-8: source/exec charset. /bigobj: large TUs (CanvasView.cpp, MainWindow.cpp)
    # exceed the COFF section limit otherwise (fatal error C1128).
    add_compile_options(/utf-8 /bigobj)
    # MSVC's <cmath> only exposes M_PI & friends when this is defined before the
    # header is parsed; several files use M_PI via plain <cmath> (glibc has it by
    # default, MSVC does not).
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

option(USE_RHI "Use QRhi rendering backend (Vulkan/Metal/D3D12)" OFF)

if(USE_RHI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network Concurrent Rhi)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    add_compile_definitions(ORION_USE_RHI)
else()
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network Concurrent)
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# zlib: brush import inflates compressed PNG text chunks (.kpp) and DEFLATE'd
# ZIP entries (.bundle). Available everywhere OpenCV/Qt already pull it in.
find_package(ZLIB REQUIRED)

# ── ONNX Runtime (optional) ──────────────────────────────────
# Single editor build; CPU/CUDA/TensorRT/etc. are runtime Execution Providers
# chosen in Settings, never separate builds. When the runtime is not found the
# editor still builds and runs — the AI layer simply reports it as unavailable.
option(ENABLE_ONNXRUNTIME "Build with ONNX Runtime AI inference support" ON)
    # CACHE PATH "Root of the vendored ONNX Runtime distribution (include/ + lib/)")

set(ONNXRUNTIME_FOUND OFF)
if(ENABLE_ONNXRUNTIME)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS "${ONNXRUNTIME_ROOT}/include")
    find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
        HINTS "${ONNXRUNTIME_ROOT}/lib")
    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        set(ONNXRUNTIME_FOUND ON)
        get_filename_component(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
        message(STATUS "ONNX Runtime: ${ONNXRUNTIME_LIBRARY}")
    else()
        message(WARNING "ENABLE_ONNXRUNTIME is ON but ONNX Runtime was not found under ONNXRUNTIME_ROOT='${ONNXRUNTIME_ROOT}'. Building without AI inference support.")
    endif()
endif()

# Single source of truth for the app version: project(... VERSION ...) above.
# Everything that displays a version (splash screen, About dialog, MCP server
# info, packaging) derives from the generated header instead of a hardcoded literal.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.hpp
    @ONLY)

add_executable(Hazor WIN32
    src/main.cpp

    src/core/Tile.hpp
    src/core/TileManager.cpp
    src/core/TileManager.hpp
    src/core/DirtyRegion.cpp
    src/core/DirtyRegion.hpp
    src/core/ViewportCamera.cpp
    src/core/ViewportCamera.hpp
    src/core/RenderScheduler.cpp
    src/core/RenderScheduler.hpp
    src/core/RasterLayerStorage.cpp
    src/core/RasterLayerStorage.hpp
    src/core/Layer.hpp
    src/core/Layer.cpp
    src/core/AdjustmentTypes.hpp
    src/core/AdjustmentTypes.cpp
    src/core/CurvesData.hpp
    src/core/CurvesData.cpp
    src/core/ColorBalanceData.hpp
    src/core/ColorBalanceData.cpp
    src/core/HueSaturationData.hpp
    src/core/HueSaturationData.cpp
    src/core/SolidColorData.hpp
    src/core/ColorEngine.cpp
    src/core/ColorEngine.hpp
    src/core/SwatchManager.cpp
    src/core/SwatchManager.hpp
    src/core/AppPaths.cpp
    src/core/AppPaths.hpp
    src/core/CrashHandler.cpp
    src/core/CrashHandler.hpp
    src/core/Document.hpp
    src/core/Document.cpp
    src/core/GuideTypes.cpp
    src/core/GuideTypes.hpp
    src/core/GuideManager.cpp
    src/core/GuideManager.hpp
    src/core/PerformanceConfig.hpp
    src/core/AlphaChannel.hpp
    src/core/ShapeTypes.hpp
    src/core/Clipboard.cpp
    src/core/Clipboard.hpp
    src/core/SelectionMask.cpp
    src/core/SelectionMask.hpp
    src/core/BoxSelection.cpp
    src/core/BoxSelection.hpp
    src/core/LayerEffect.cpp

    # Animation model (Etapa 4): document-owned keyframe/track data.
    src/animation/AnimationTrack.cpp
    src/animation/AnimationModel.cpp
    # Sparse raster cel tracks + shared/COW cel payload storage (Etapa 13).
    src/animation/RasterCelModel.cpp
    # Interpolation + transform decompose/compose (Etapa 5) and evaluator (Etapa 6).
    src/animation/AnimationInterpolation.cpp
    src/animation/AnimationTransform.cpp
    src/animation/AnimationEvaluator.cpp
    # Time-based playback backend (Etapa 11).
    src/animation/PlaybackController.cpp
    # Isolated CPU image-sequence export (Etapa 12).
    src/animation/AnimationExportService.cpp
    # Property-edit controller with auto-key (Etapa 8).
    src/animation/LayerPropertyController.cpp
    src/core/LayerEffect.hpp

    src/gradient/GradientTypes.cpp
    src/gradient/GradientTypes.hpp
    src/gradient/GradientRenderer.cpp
    src/gradient/GradientRenderer.hpp
    src/gradient/GradientPresetManager.cpp
    src/gradient/GradientPresetManager.hpp
    src/gradient/GradientCommand.cpp
    src/gradient/GradientCommand.hpp

    src/engine/ImageEngine.cpp
    src/engine/ImageEngine.hpp
    src/engine/ColorSamplerService.cpp
    src/engine/ColorSamplerService.hpp
    src/engine/ShapeRenderer.cpp
    src/engine/ShapeRenderer.hpp
    src/engine/ShapeVectorRenderer.cpp
    src/engine/ShapeVectorRenderer.hpp

    src/controller/ImageController.cpp
    src/controller/ImageController_Tools.cpp
    src/controller/ImageController.hpp
    src/controller/CommandHistory.cpp
    src/controller/CommandHistory.hpp

    src/tools/ToolCall.hpp
    src/shape/ShapePresetFactory.cpp
    src/shape/ShapePresetFactory.hpp
    src/shape/ShapeLayerUpdater.cpp
    src/shape/ShapeLayerUpdater.hpp
    src/shape/ShapeIconLibrary.cpp
    src/shape/ShapeIconLibrary.hpp
    src/shape/SvgShapeConverter.cpp
    src/shape/SvgShapeConverter.hpp
    src/shape/ShapeCommands.hpp
    src/transform/TransformTypes.hpp
    src/transform/SnapEngine.cpp
    src/transform/SnapEngine.hpp
    src/transform/TransformController.cpp
    src/transform/TransformController.hpp
    src/tools/ToolCatalog.cpp
    src/tools/ToolCatalog.hpp
    src/tools/ToolExecutor.cpp
    src/tools/ToolExecutor.hpp

    src/agent/AgentConfig.hpp
    src/agent/AgentPresetManager.cpp
    src/agent/AgentPresetManager.hpp
    src/agent/LLMClient.cpp
    src/agent/LLMClient.hpp
    src/agent/AgentController.cpp
    src/agent/AgentController.hpp
    src/agent/AgentPanel.cpp
    src/agent/AgentPanel.hpp

    src/ai/InpaintTypes.hpp
    src/ai/ImageGenProvider.hpp
    src/ai/ImageGenProviderFactory.cpp
    src/ai/ImageGenProviderFactory.hpp
    src/ai/A1111ImageGenProvider.cpp
    src/ai/A1111ImageGenProvider.hpp
    src/ai/OpenAIImageGenProvider.cpp
    src/ai/OpenAIImageGenProvider.hpp
    src/ai/StabilityImageGenProvider.cpp
    src/ai/StabilityImageGenProvider.hpp
    src/ai/GeminiImageGenProvider.cpp
    src/ai/GeminiImageGenProvider.hpp
    src/ai/AggregatorImageGenProvider.cpp
    src/ai/AggregatorImageGenProvider.hpp
    src/ai/PromptAssistProvider.cpp
    src/ai/PromptAssistProvider.hpp

    # ── AI inference (ONNX) infrastructure — Etapa 1 ──
    src/ai/runtime/AiRuntimeSettings.hpp
    src/ai/runtime/AiRuntimeSettings.cpp
    src/ai/runtime/AiDeviceInfo.hpp
    src/ai/runtime/AiExecutionProvider.hpp
    src/ai/runtime/AiRuntimeTypes.hpp
    src/ai/runtime/AiInferenceSession.hpp
    src/ai/runtime/AiInferenceSession.cpp
    src/ai/runtime/AiSessionBundle.hpp
    src/ai/runtime/AiSessionBundle.cpp
    src/ai/runtime/AiRuntimeManager.hpp
    src/ai/runtime/AiRuntimeManager.cpp
    src/ai/onnx/OnnxSessionHandle.hpp
    src/ai/onnx/OnnxSessionHandle.cpp
    src/ai/onnx/OnnxRuntimeBackend.hpp
    src/ai/onnx/OnnxRuntimeBackend.cpp
    src/ai/models/AiModelTypes.hpp
    src/ai/models/AiModelDescriptor.hpp
    src/ai/models/AiModelDescriptor.cpp
    src/ai/models/AiModelJson.hpp
    src/ai/models/AiModelJson.cpp
    src/ai/models/AiModelManifest.hpp
    src/ai/models/AiModelManifest.cpp
    src/ai/models/AiModelRegistry.hpp
    src/ai/models/AiModelRegistry.cpp
    src/ai/models/AiModelDownloadManager.hpp
    src/ai/models/AiModelDownloadManager.cpp
    src/ai/upscale/UpscaleTypes.hpp
    src/ai/upscale/UpscaleBackend.hpp
    src/ai/upscale/RealEsrganProcessBackend.hpp
    src/ai/upscale/RealEsrganProcessBackend.cpp
    src/ai/upscale/UpscaleService.hpp
    src/ai/upscale/UpscaleService.cpp
    src/ui/AiSettingsPage.hpp
    src/ui/AiSettingsPage.cpp
    src/ui/AiAlertService.hpp
    src/ui/AiAlertService.cpp

    # ── AI compatibility layer (platform / runtime / providers / CUDA / models) ──
    src/ai/runtime/AiRuntimeError.hpp
    src/ai/compat/AiCompatibilityReport.h
    src/ai/compat/AiPlatformDetector.hpp
    src/ai/compat/AiPlatformDetector.cpp
    src/ai/compat/AiOsCompatibilityProfile.hpp
    src/ai/compat/AiOsCompatibilityProfile.cpp
    src/ai/compat/AiRuntimeCompatibilityProfile.hpp
    src/ai/compat/AiRuntimeCompatibilityProfile.cpp
    src/ai/compat/AiCudaCompatibilityChecker.hpp
    src/ai/compat/AiCudaCompatibilityChecker.cpp
    src/ai/compat/AiCudnnCompatibilityChecker.hpp
    src/ai/compat/AiCudnnCompatibilityChecker.cpp
    src/ai/compat/AiProviderCompatibilityChecker.hpp
    src/ai/compat/AiProviderCompatibilityChecker.cpp
    src/ai/compat/AiModelCompatibilityChecker.hpp
    src/ai/compat/AiModelCompatibilityChecker.cpp
    src/ai/compat/AiCompatibilityManager.hpp
    src/ai/compat/AiCompatibilityManager.cpp

    # ── AI selection / SAM segmentation — Etapa 2 ──
    src/ai/AiSelectionTypes.hpp
    src/ai/AiRemoveTypes.hpp
    src/ai/AiImageSnapshot.hpp
    src/ai/AiImageSnapshot.cpp
    src/ai/AiJobTypes.hpp
    src/ai/AiMaskPipeline.hpp
    src/ai/AiMaskPipeline.cpp
    src/ai/inpaint/AiInpaintTypes.hpp
    src/ai/inpaint/AiInpaintPreprocessor.hpp
    src/ai/inpaint/AiInpaintPreprocessor.cpp
    src/ai/inpaint/AiInpaintPipeline.hpp
    src/ai/inpaint/AiInpaintPipeline.cpp
    src/ai/inpaint/AiDirectInpaintModel.hpp
    src/ai/inpaint/AiDirectInpaintModel.cpp
    src/ai/inpaint/StableDiffusionLatentUtils.hpp
    src/ai/inpaint/StableDiffusionLatentUtils.cpp
    src/ai/inpaint/StableDiffusionScheduler.hpp
    src/ai/inpaint/StableDiffusionScheduler.cpp
    src/ai/inpaint/StableDiffusionTokenizer.hpp
    src/ai/inpaint/StableDiffusionTokenizer.cpp
    src/ai/inpaint/StableDiffusionOnnxPipeline.hpp
    src/ai/inpaint/StableDiffusionOnnxPipeline.cpp
    src/ai/AiJobRunner.hpp
    src/ai/AiJobRunner.cpp
    src/ai/AiSubjectSelector.hpp
    src/ai/AiSubjectSelector.cpp
    src/ai/sam/SamTypes.hpp
    src/ai/sam/SamModelConfig.hpp
    src/ai/sam/SamModelConfig.cpp
    src/ai/sam/SamCoordinateMapper.hpp
    src/ai/sam/SamPreprocessor.hpp
    src/ai/sam/SamPreprocessor.cpp
    src/ai/sam/SamPostprocessor.hpp
    src/ai/sam/SamPostprocessor.cpp
    src/ai/sam/SamEmbeddingCache.hpp
    src/ai/sam/SamEmbeddingCache.cpp
    src/ai/sam/Sam1Segmenter.hpp
    src/ai/sam/Sam1Segmenter.cpp
    src/ai/tool/AiObjectSelectionController.hpp
    src/ai/tool/AiObjectSelectionController.cpp
    src/ai/tool/AiRemoveObjectController.hpp
    src/ai/tool/AiRemoveObjectController.cpp

    # ── AI refinement / matting + background removal — Etapa 3 ──
    src/ai/AiMaskPostProcessor.hpp
    src/ai/AiMaskPostProcessor.cpp
    src/ai/matting/AiRefineTypes.hpp
    src/ai/matting/AiMattingModel.hpp
    src/ai/matting/MattingModelConfig.hpp
    src/ai/matting/MattingModelConfig.cpp
    src/ai/matting/OnnxAlphaModel.hpp
    src/ai/matting/OnnxAlphaModel.cpp
    src/ai/matting/BiRefNetMattingModel.hpp
    src/ai/matting/BiRefNetMattingModel.cpp
    src/ai/matting/ModNetMattingModel.hpp
    src/ai/matting/ModNetMattingModel.cpp
    src/ai/matting/RmbgBackgroundRemovalModel.hpp
    src/ai/matting/RmbgBackgroundRemovalModel.cpp
    src/ai/matting/AiRefineModelFactory.hpp
    src/ai/matting/AiRefineModelFactory.cpp

    src/mcp/McpServer.cpp
    src/mcp/McpServer.hpp
    src/mcp/McpSettings.hpp
    src/mcp/HttpParser.cpp
    src/mcp/HttpParser.hpp
    src/mcp/McpTypes.hpp

    src/color/AdvancedIccColorEngine.hpp
    src/color/ColorEngine.hpp
    src/color/ColorManagementService.cpp
    src/color/ColorManagementService.hpp
    src/color/ColorManagementSettings.hpp
    src/color/ColorProfile.cpp
    src/color/ColorProfile.hpp
    src/color/ColorProfileRepository.cpp
    src/color/ColorProfileRepository.hpp
    src/color/DisplayProfileService.cpp
    src/color/DisplayProfileService.hpp
    src/color/QtColorEngine.cpp
    src/color/QtColorEngine.hpp
    src/color/SoftProofSettings.hpp

    src/io/ImageIO.cpp
    src/io/ImageIO.hpp
    src/io/ImageCodec.cpp
    src/io/ImageCodec.hpp
    src/io/ProjectFileService.cpp
    src/io/ProjectFileService.hpp

    src/brush/BrushTypes.hpp
    src/brush/DabContext.hpp
    src/brush/Brush.hpp
    src/brush/AutoBrush.cpp
    src/brush/AutoBrush.hpp
    src/brush/PredefinedBrush.cpp
    src/brush/PredefinedBrush.hpp
    src/brush/BrushDab.cpp
    src/brush/BrushDab.hpp
    src/brush/BrushRenderer.cpp
    src/brush/BrushRenderer.hpp
    src/brush/BrushEngine.cpp
    src/brush/BrushEngine.hpp
    src/brush/BrushInputState.hpp
    src/brush/DynamicsConfig.hpp
    src/brush/DynamicsEvaluator.cpp
    src/brush/DynamicsEvaluator.hpp
    src/brush/BrushPreset.hpp
    src/brush/BrushPresetManager.cpp
    src/brush/BrushPresetManager.hpp
    src/brush/BrushTextureLibrary.cpp
    src/brush/BrushTextureLibrary.hpp
    src/brush/BrushTipLibrary.cpp
    src/brush/BrushTipLibrary.hpp
    src/brush/BrushPreviewRenderer.cpp
    src/brush/BrushPreviewRenderer.hpp
    src/brush/BrushPreviewCache.cpp
    src/brush/BrushPreviewCache.hpp

    src/brush/import/BrushImportTypes.hpp
    src/brush/import/IBrushImportAdapter.hpp
    src/brush/import/BrushImportManager.cpp
    src/brush/import/BrushImportManager.hpp
    src/brush/import/BrushImportArchive.cpp
    src/brush/import/BrushImportArchive.hpp
    src/brush/import/KritaKppImportAdapter.cpp
    src/brush/import/KritaKppImportAdapter.hpp
    src/brush/import/KritaBundleImportAdapter.cpp
    src/brush/import/KritaBundleImportAdapter.hpp
    src/brush/import/GimpBrushDecoder.cpp
    src/brush/import/GimpBrushDecoder.hpp
    src/brush/import/GimpBrushImportAdapter.cpp
    src/brush/import/GimpBrushImportAdapter.hpp
    src/brush/import/ImageBrushTipImportAdapter.cpp
    src/brush/import/ImageBrushTipImportAdapter.hpp
    src/brush/import/BrushPresetConverter.cpp
    src/brush/import/BrushPresetConverter.hpp

    src/text/TextTypes.hpp
    src/text/TextLayoutEngine.cpp
    src/text/TextLayoutEngine.hpp
    src/text/TextRenderer.cpp
    src/text/TextRenderer.hpp
    src/text/TextEditorController.cpp
    src/text/TextEditorController.hpp

    src/renderer/RenderBackend.hpp
    src/renderer/OpenGLBackend.hpp
    src/renderer/RenderParams.hpp
    src/renderer/RenderContext.hpp
    src/renderer/DocumentCompositor.hpp
    src/renderer/DocumentCompositor.cpp
    src/renderer/BlendRules.hpp
    src/renderer/BlendMath.hpp
    src/renderer/BlendShaderLib.hpp
    src/renderer/ProjectionCache.hpp
    src/renderer/ProjectionCache.cpp
    src/renderer/AsyncProjectionBuilder.hpp
    src/renderer/AsyncProjectionBuilder.cpp
    src/renderer/GPUViewport.cpp
    src/renderer/GPUViewport_Shaders.cpp
    src/renderer/GPUViewport_Overlays.cpp
    src/renderer/GPUViewport_Crop.cpp
    src/renderer/GPUViewport.hpp
    src/renderer/LayerCompositor.cpp
    src/renderer/LayerCompositor.hpp
    src/renderer/RulerGuideOverlay.cpp
    src/renderer/RulerGuideOverlay.hpp
    src/renderer/TileRenderer.cpp
    src/renderer/TileRenderer.hpp
    src/renderer/MipmapCache.cpp
    src/renderer/MipmapCache.hpp
    src/renderer/CanvasView.cpp
    src/renderer/CanvasView.hpp

    src/cache/RenderCache.cpp
    src/cache/RenderCache.hpp

    src/async/AsyncJob.hpp
    src/async/AsyncJobSystem.cpp
    src/async/AsyncJobSystem.hpp

    src/processing/FilterProcessor.cpp
    src/processing/FilterProcessor.hpp
    src/processing/PreviewRenderer.cpp
    src/processing/PreviewRenderer.hpp

    src/memory/BufferPool.cpp
    src/memory/BufferPool.hpp

    src/resources/resources.qrc

    src/ui/LayerPanel.cpp
    src/ui/LayerItemDelegate.cpp
    src/ui/LayerItemDelegate.hpp
    src/ui/LayerStylesDialog.cpp
    src/ui/LayerStylesDialog.hpp
    src/ui/ColorPanel.cpp
    src/ui/ColorPanel.hpp
    src/ui/SwatchesPanel.cpp
    src/ui/SwatchesPanel.hpp
    src/ui/AppComboBox.cpp
    src/ui/AppComboBox.hpp
    src/ui/ColorMixerPanel.cpp
    src/ui/ColorMixerPanel.hpp
    src/ui/ColorPaletteBar.cpp
    src/ui/ColorPaletteBar.hpp
    src/ui/ZoomComboBox.cpp
    src/ui/ZoomComboBox.hpp
    src/ui/ColorSwapWidget.cpp
    src/ui/ColorSwapWidget.hpp
    src/ui/colorpicker/ColorPickerDialog.cpp
    src/ui/colorpicker/ColorPickerDialog.hpp
    src/ui/colorpicker/SaturationBrightnessArea.cpp
    src/ui/colorpicker/SaturationBrightnessArea.hpp
    src/ui/colorpicker/ColorHueSlider.cpp
    src/ui/colorpicker/ColorHueSlider.hpp
    src/ui/colorpicker/ColorPreviewWidget.cpp
    src/ui/colorpicker/ColorPreviewWidget.hpp
    src/ui/colorpicker/ColorFieldsWidget.cpp
    src/ui/colorpicker/ColorFieldsWidget.hpp
    src/ui/color/AssignProfileDialog.cpp
    src/ui/color/AssignProfileDialog.hpp
    src/ui/color/ConvertToProfileDialog.cpp
    src/ui/color/ConvertToProfileDialog.hpp
    src/ui/color/MissingProfileDialog.cpp
    src/ui/color/MissingProfileDialog.hpp
    src/ui/color/ProfileMismatchDialog.cpp
    src/ui/color/ProfileMismatchDialog.hpp
    src/ui/color/SoftProofDialog.cpp
    src/ui/color/SoftProofDialog.hpp
    src/ui/DropdownSlider.cpp
    src/ui/DropdownSlider.hpp
    src/ui/GradientSlider.cpp
    src/ui/GradientSlider.hpp
    src/ui/ScrubbableValueInput.cpp
    src/ui/ScrubbableValueInput.h
    src/ui/ProgressDialog.cpp
    src/ui/ProgressDialog.h
    src/ui/AgentConfigWidget.cpp
    src/ui/AgentConfigWidget.hpp
    src/ui/AiUpscaleDialog.cpp
    src/ui/AiUpscaleDialog.hpp
    src/ui/GenerativeFillDialog.cpp
    src/ui/GenerativeFillDialog.hpp
    src/ui/AppDiagnostics.hpp
    src/ui/AppDiagnostics.cpp
    src/ui/AboutDialog.hpp
    src/ui/AboutDialog.cpp
    src/ui/AppSettingsDialog.cpp
    src/ui/AppSettingsDialog.hpp
    src/ui/ColorSettingsPage.cpp
    src/ui/ColorSettingsPage.hpp
    src/ui/GeneralSettingsPage.cpp
    src/ui/GeneralSettingsPage.hpp
    src/ui/McpSettingsPage.cpp
    src/ui/McpSettingsPage.hpp
    src/ui/RulerGuideSettingsPage.cpp
    src/ui/RulerGuideSettingsPage.hpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow_Menus.cpp
    src/ui/MainWindow.hpp
    src/ui/ShortcutManager.cpp
    src/ui/ShortcutManager.hpp
    src/ui/ShortcutSettingsPage.cpp
    src/ui/ShortcutSettingsPage.hpp
    src/ui/BrushDynamicsPanel.cpp
    src/ui/BrushDynamicsPanel.hpp
    src/ui/CheckableRow.cpp
    src/ui/CheckableRow.hpp
    src/ui/BrushTexturePanel.cpp
    src/ui/BrushTexturePanel.hpp
    src/ui/BrushResourceBrowser.cpp
    src/ui/BrushResourceBrowser.hpp
    src/ui/BrushCurveOptionEditor.cpp
    src/ui/BrushCurveOptionEditor.hpp
    src/ui/BrushPresetPicker.cpp
    src/ui/BrushPresetPicker.hpp
    src/ui/BrushPanel.cpp
    src/ui/BrushPanel.hpp
    src/ui/BrushLibraryListView.cpp
    src/ui/BrushLibraryListView.hpp
    src/ui/CustomShapesPanel.cpp
    src/ui/CustomShapesPanel.hpp
    src/ui/BrushLibraryTreeModel.cpp
    src/ui/BrushLibraryTreeModel.hpp
    src/ui/BrushPresetListModel.cpp
    src/ui/BrushPresetListModel.hpp
    src/ui/BrushPresetItemDelegate.cpp
    src/ui/BrushPresetItemDelegate.hpp
    src/ui/BrushTipPreview.cpp
    src/ui/BrushTipPreview.hpp
    src/ui/brush/BrushImportDialog.cpp
    src/ui/brush/BrushImportDialog.hpp
    src/ui/gradient/GradientPreviewWidget.cpp
    src/ui/gradient/GradientPreviewWidget.hpp
    src/ui/gradient/GradientStopEditor.cpp
    src/ui/gradient/GradientStopEditor.hpp
    src/ui/gradient/GradientEditorDialog.cpp
    src/ui/gradient/GradientEditorDialog.hpp
    src/ui/ExportImageDialog.cpp
    src/ui/ExportImageDialog.hpp
    src/ui/NewDocumentDialog.cpp
    src/ui/NewDocumentDialog.hpp
    src/ui/RefineEdgeDialog.cpp
    src/ui/RefineEdgeDialog.hpp
    src/ui/TitleBar.cpp
    src/ui/TitleBar.hpp
    src/ui/SplashScreen.cpp
    src/ui/SplashScreen.hpp
    src/ui/TransformFieldsWidget.cpp
    src/ui/TransformFieldsWidget.hpp
    src/ui/PropertiesPanel.cpp
    src/ui/PropertiesPanel.hpp
    src/ui/CurvesEditorWidget.cpp
    src/ui/CurvesEditorWidget.hpp
    src/ui/AdjustmentPanelBar.cpp
    src/ui/AdjustmentPanelBar.hpp
    src/ui/ColorBalanceAdjustmentWidget.cpp
    src/ui/ColorBalanceAdjustmentWidget.hpp
    src/ui/HueSaturationAdjustmentWidget.cpp
    src/ui/HueSaturationAdjustmentWidget.hpp
    src/ui/SolidColorAdjustmentWidget.cpp
    src/ui/SolidColorAdjustmentWidget.hpp
    src/ui/HistogramPanel.cpp
    src/ui/TimelinePanel.cpp
    src/ui/HistogramPanel.hpp
    src/ui/HistogramWidget.cpp
    src/ui/HistogramWidget.hpp
    src/ui/HistogramStatsWidget.cpp
    src/ui/HistogramStatsWidget.hpp
    src/histogram/HistogramGenerator.cpp
    src/histogram/HistogramGenerator.hpp
    src/histogram/HistogramCacheManager.cpp
    src/histogram/HistogramCacheManager.hpp
    src/histogram/HistogramRenderer.cpp
    src/histogram/HistogramRenderer.hpp
    src/histogram/HistogramTypes.hpp
    src/ui/HistoryPanel.cpp
    src/ui/HistoryPanel.hpp
    src/ui/AlignBar.cpp
    src/ui/AlignBar.hpp
    src/ui/FlowLayout.cpp
    src/ui/FlowLayout.hpp
    src/ui/ToolOptionsBar.cpp
    src/ui/ToolOptionsBar.hpp
    src/ui/PopupPanel.cpp
    src/ui/PopupPanel.hpp
    src/ui/ToolsPanel.cpp
    src/ui/ToolsPanel.hpp
    src/ui/FillLayerDialog.cpp
    src/ui/FillLayerDialog.hpp
    src/ui/ResizeLayerDialog.cpp
    src/ui/ResizeLayerDialog.hpp
    src/ui/ImageSizeDialog.cpp
    src/ui/ImageSizeDialog.hpp
    src/ui/CanvasSizeDialog.cpp
    src/ui/CanvasSizeDialog.hpp
    src/ui/dialogs/ToolDialog.cpp
    src/ui/dialogs/ToolDialog.hpp
    src/ui/dialogs/ParamRow.cpp
    src/ui/dialogs/ParamRow.hpp
    src/ui/dialogs/AdjustColorDialog.cpp
    src/ui/dialogs/AdjustColorDialog.hpp
    src/ui/dialogs/GaussianBlurDialog.cpp
    src/ui/dialogs/GaussianBlurDialog.hpp
    src/ui/dialogs/SharpenDialog.cpp
    src/ui/dialogs/SharpenDialog.hpp
    src/ui/dialogs/MedianBlurDialog.cpp
    src/ui/dialogs/MedianBlurDialog.hpp
    src/ui/dialogs/BoxBlurDialog.cpp
    src/ui/dialogs/BoxBlurDialog.hpp
    src/ui/dialogs/BilateralBlurDialog.cpp
    src/ui/dialogs/BilateralBlurDialog.hpp
    src/ui/dialogs/MotionBlurDialog.cpp
    src/ui/dialogs/MotionBlurDialog.hpp
    src/ui/dialogs/RadialBlurDialog.cpp
    src/ui/dialogs/RadialBlurDialog.hpp
    src/ui/dialogs/ZoomBlurDialog.cpp
    src/ui/dialogs/ZoomBlurDialog.hpp
    src/ui/dialogs/EdgeDetectDialog.cpp
    src/ui/dialogs/EdgeDetectDialog.hpp
    src/ui/dialogs/PosterizeDialog.cpp
    src/ui/dialogs/PosterizeDialog.hpp
    src/ui/dialogs/ThresholdDialog.cpp
    src/ui/dialogs/ThresholdDialog.hpp
    src/ui/dialogs/NoiseReduceDialog.cpp
    src/ui/dialogs/NoiseReduceDialog.hpp
    src/ui/dialogs/RemoveBgDialog.cpp
    src/ui/dialogs/RemoveBgDialog.hpp
)

file(GLOB_RECURSE IMAGE_EDITOR_SHAPE_SVG_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/shapes/*.svg"
)

qt_add_resources(Hazor "shape_icons"
    PREFIX "/shapes"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/shapes"
    FILES ${IMAGE_EDITOR_SHAPE_SVG_FILES}
)

target_link_libraries(Hazor PRIVATE
    theme_lib
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZLIB::ZLIB
)

if(UNIX AND NOT APPLE)
    target_link_libraries(Hazor PRIVATE dl)
    target_link_options(Hazor PRIVATE -rdynamic)
endif()

if(WIN32)
    target_link_libraries(Hazor PRIVATE opengl32)
    # GUI subsystem (no console window) via add_executable(WIN32). Keep plain
    # main() instead of requiring a WinMain / Qt6::EntryPoint: /ENTRY:mainCRTStartup
    # uses the CRT entry that sets up argc/argv and calls main() under the
    # WINDOWS subsystem.
    target_link_options(Hazor PRIVATE /ENTRY:mainCRTStartup)
    # Embed the application icon into Hazor.exe (shows in Explorer, taskbar,
    # alt-tab). The .ico is pre-generated from app-icon.png by
    # tools/generate_app_icon_ico.py and versioned in the repo, so the build does
    # no conversion. MSVC's rc compiler handles .rc files added as sources.
    target_sources(Hazor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/app-icon.rc)
endif()

if(USE_RHI)
    target_link_libraries(Hazor PRIVATE Qt6::Rhi Qt6::GuiPrivate)
endif()

target_include_directories(Hazor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${OpenCV_INCLUDE_DIRS}
)

# ── AI compatibility layer config ────────────────────────────
# HAZOR_SOURCE_DIR lets AppPaths::thirdPartyDir() resolve the read-only 3rdparty
# tree (bundled models, ONNX Runtime, RealESRGAN) from the source checkout during
# dev runs. CUDA major is the single knob that drives the CUDA/cuDNN requirement
# messaging (spec §7). All distributed assets are resolved relative to the
# executable (3rdparty/...) — no absolute paths are baked into the binary.
if(NOT DEFINED HAZOR_ONNX_CUDA_MAJOR)
    set(HAZOR_ONNX_CUDA_MAJOR 12)
endif()
target_compile_definitions(Hazor PRIVATE
    HAZOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    HAZOR_ONNX_CUDA_MAJOR=${HAZOR_ONNX_CUDA_MAJOR})

# ── ONNX Runtime link + headers + rpath ──────────────────────
if(ONNXRUNTIME_FOUND)
    target_compile_definitions(Hazor PRIVATE HAVE_ONNXRUNTIME=1)
    target_include_directories(Hazor PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(Hazor PRIVATE ${ONNXRUNTIME_LIBRARY})
    # Locate libonnxruntime.so and its provider_shared/cuda/tensorrt siblings at
    # runtime without a separate build or install step (dev convenience).
    set_target_properties(Hazor PROPERTIES
        BUILD_RPATH "${ONNXRUNTIME_LIB_DIR}"
        INSTALL_RPATH "${ONNXRUNTIME_LIB_DIR}")
endif()

# Theme sources — object library for reuse by tests
set(THEME_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/Theme.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ClassicTheme.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ClassicTheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/DarkerTheme.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/DarkerTheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ThemeManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ThemeManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ComponentStyle.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ComponentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/QtTheme.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ThemeBuilder.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ThemeBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ScrollBarStyle.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme/ScrollBarStyle.cpp
)
add_library(theme_lib STATIC ${THEME_SOURCES})
target_include_directories(theme_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(theme_lib PUBLIC Qt6::Widgets)

# ── Testing ──────────────────────────────────────────────────
option(BUILD_TESTS "Build unit and UI tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()
