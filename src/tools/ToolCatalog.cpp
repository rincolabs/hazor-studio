#include "ToolCatalog.hpp"
#include <sstream>
#include <algorithm>

std::vector<ToolDefinition> ToolCatalog::s_catalog = buildCatalog();

std::vector<ToolDefinition> ToolCatalog::buildCatalog()
{
    return {
        {"new_document", "Create a new document", "Document",
            {{"width", "int", 1, 10000, 1920},
             {"height", "int", 1, 10000, 1080}}},
        {"resize_document", "Resize the document canvas", "Document",
            {{"width", "int", 1, 10000, 1920},
             {"height", "int", 1, 10000, 1080}}},
        {"resize_image", "Resample the whole image to a new pixel size. "
            "interpolation: 'nearest', 'bilinear', 'bicubic', 'lanczos' or "
            "'bicubic_automatic' (default). resample=1 resamples pixels, "
            "scale_styles=1 scales layer styles.", "Document",
            {{"width", "int", 1, 10000, 1920},
             {"height", "int", 1, 10000, 1080},
             {"resample", "int", 0, 1, 1},
             {"scale_styles", "int", 0, 1, 1},
             {"interpolation", "string", 0, 0, 0}}},
        {"resize_canvas", "Resize the canvas without resampling layers. "
            "anchor: 'top_left','top_center','top_right','middle_left','center',"
            "'middle_right','bottom_left','bottom_center','bottom_right'. "
            "fill_extension=1 fills new area with extension_color (e.g. '#ffffff').", "Document",
            {{"width", "int", 1, 10000, 1920},
             {"height", "int", 1, 10000, 1080},
             {"fill_extension", "int", 0, 1, 0},
             {"anchor", "string", 0, 0, 0},
             {"extension_color", "string", 0, 0, 0}}},

        {"add_layer", "Add a new empty layer", "Layer", {}},
        {"remove_layer", "Remove a layer by index", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"duplicate_layer", "Duplicate a layer", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"set_layer_opacity", "Set layer opacity (0-1)", "Layer",
            {{"index", "int", -1, 100, -1},
             {"opacity", "float", 0, 1, 1}}},
        {"set_layer_visibility", "Show or hide a layer", "Layer",
            {{"index", "int", -1, 100, -1},
             {"visible", "int", 0, 1, 1}}},
        {"fill_layer", "Fill active layer with a color", "Layer",
            {{"red", "int", 0, 255, 255},
             {"green", "int", 0, 255, 255},
             {"blue", "int", 0, 255, 255},
             {"alpha", "int", 0, 255, 255}}},
        {"fill_bucket", "Fill connected region with color (paint bucket)", "Filter",
            {{"x", "int", 0, 100000, 0},
             {"y", "int", 0, 100000, 0},
             {"red", "int", 0, 255, 255},
             {"green", "int", 0, 255, 255},
             {"blue", "int", 0, 255, 255},
             {"alpha", "int", 0, 255, 255},
             {"tolerance", "float", 0, 1, 0}}},

        {"adjust_color", "Adjust brightness, contrast, saturation, and hue", "Adjust",
            {{"brightness", "float", -1, 1, 0},
             {"contrast", "float", -1, 1, 0},
             {"saturation", "float", -1, 1, 0},
             {"hue", "float", -1, 1, 0},
             {"auto_contrast", "int", 0, 1, 0}}},
        {"adjust_brightness", "Adjust image brightness (-1 to 1)", "Adjust",
            {{"value", "float", -1, 1, 0}}},
        {"adjust_contrast", "Adjust image contrast (-1 to 1)", "Adjust",
            {{"value", "float", -1, 1, 0}}},
        {"adjust_saturation", "Adjust color saturation (-1 to 1)", "Adjust",
            {{"value", "float", -1, 1, 0}}},
        {"adjust_hue", "Shift hue rotation (-1 to 1)", "Adjust",
            {{"value", "float", -1, 1, 0}}},

        {"gaussian_blur", "Apply Gaussian blur", "Filter",
            {{"radius", "float", 0, 100, 3}}},
        {"sharpen", "Sharpen the image", "Filter",
            {{"value", "float", 0, 5, 1}}},
        {"median_blur", "Apply median blur", "Filter",
            {{"ksize", "int", 1, 31, 5}}},
        {"box_blur", "Apply box (average) blur", "Filter",
            {{"radius", "int", 0, 100, 3}}},
        {"bilateral_blur", "Edge-preserving bilateral blur", "Filter",
            {{"diameter", "int", 1, 50, 9},
             {"sigma_color", "float", 1, 200, 75},
             {"sigma_space", "float", 1, 200, 75}}},
        {"motion_blur", "Directional motion blur", "Filter",
            {{"length", "int", 1, 200, 15},
             {"angle", "float", 0, 360, 0}}},
        {"radial_blur", "Rotational blur around a center", "Filter",
            {{"amount", "float", 0, 1, 0.25},
             {"samples", "int", 2, 64, 12},
             {"center_x", "float", 0, 1, 0.5},
             {"center_y", "float", 0, 1, 0.5}}},
        {"zoom_blur", "Zoom blur toward a center", "Filter",
            {{"amount", "float", 0, 1, 0.25},
             {"samples", "int", 2, 64, 12},
             {"center_x", "float", 0, 1, 0.5},
             {"center_y", "float", 0, 1, 0.5}}},
        {"edge_detect", "Detect edges using Canny", "Filter",
            {{"threshold1", "float", 0, 500, 50},
             {"threshold2", "float", 0, 500, 150}}},

        {"crop", "Crop the active layer", "Transform",
            {{"x", "int", 0, 10000, 0},
             {"y", "int", 0, 10000, 0},
             {"width", "int", 1, 10000, 512},
             {"height", "int", 1, 10000, 512}}},
        {"rotate", "Rotate the image by angle in degrees", "Transform",
            {{"angle", "float", -360, 360, 0}}},
        {"flip_horizontal", "Flip image horizontally", "Transform", {}},
        {"flip_vertical", "Flip image vertically", "Transform", {}},
        {"resize_layer", "Resize the active layer", "Transform",
            {{"width", "int", 1, 10000, 512},
             {"height", "int", 1, 10000, 512}}},

        {"grayscale", "Convert to black and white", "Filter", {}},
        {"invert_colors", "Invert all colors", "Filter", {}},
        {"auto_contrast", "Auto-adjust contrast via histogram equalization", "Adjust", {}},
        {"noise_reduce", "Reduce noise while preserving edges", "Filter",
            {{"strength", "float", 0, 10, 2}}},
        {"posterize", "Reduce number of color levels", "Filter",
            {{"levels", "int", 2, 32, 8}}},
        {"threshold", "Convert to black and white at a cutoff value", "Filter",
            {{"value", "float", 0, 255, 128}}},
        {"remove_background", "Simple background removal based on dominant color", "Filter", {}},

        {"generative_fill", "AI generative fill / inpaint inside the active selection "
            "(requires an active selection and a configured Generative preset). "
            "mode: 'selection' writes to the active layer, 'new_layer' adds a layer.", "AI",
            {{"prompt", "string", 0, 0, 0},
             {"negative_prompt", "string", 0, 0, 0},
             {"mode", "string", 0, 0, 0},
             {"strength", "float", 0, 1, 0.75},
             {"steps", "int", 1, 150, 20},
             {"seed", "int", -1, 2147483647, -1}}},

        {"set_layer_blend_mode", "Set layer blend mode (0=Normal,1=Multiply,2=Screen,3=Overlay,4=Darken,5=Lighten,6=ColorDodge,7=ColorBurn,8=HardLight,9=SoftLight,10=Difference,11=Exclusion,12=Hue,13=Saturation,14=Color,15=Luminosity)", "Layer",
            {{"index", "int", -1, 100, -1},
             {"mode", "int", 0, 15, 0}}},
        {"float_selection", "Create new layer from current selection (cut=1 removes from source)", "Selection",
            {{"cut", "int", 0, 1, 0}}},
        {"delete_selected", "Delete (clear to transparent) the pixels within the active selection", "Selection", {}},
        {"copy", "Copy active layer, group, or selection to clipboard", "Layer", {}},
        {"paste", "Paste clipboard content as new layer(s)", "Layer",
            {{"offset", "int", 0, 1, 1}}},
        {"merge_visible", "Merge all visible layers into the active layer", "Layer", {}},
        {"merge_down", "Merge the active layer into the layer directly below it", "Layer", {}},
        {"merge_layers", "Merge all visible layers (alias of merge_visible)", "Layer", {}},
        {"flatten_image", "Flatten the whole document into a single layer", "Layer", {}},
        {"rasterize_layer", "Rasterize the active layer (convert text/shape/vector to pixels)", "Layer", {}},
        {"zoom", "Set zoom level", "Viewport",
            {{"level", "float", 0.1, 10, 1}}},
        {"reset_view", "Reset zoom and pan to default", "Viewport", {}},

        {"select_all", "Select entire canvas", "Selection", {}},
        {"deselect", "Clear current selection", "Selection", {}},
        {"select_invert", "Invert current selection", "Selection", {}},
        {"select_rect", "Create a rectangular selection", "Selection",
            {{"x", "int", 0, 100000, 0}, {"y", "int", 0, 100000, 0},
             {"width", "int", 1, 100000, 512}, {"height", "int", 1, 100000, 512},
             {"mode", "int", 0, 3, 0}}},
        {"select_magic_wand", "Select pixels by color similarity from a seed point (x,y in document pixels, like select_rect)", "Selection",
            {{"x", "int", 0, 100000, 0}, {"y", "int", 0, 100000, 0},
             {"tolerance", "float", 0, 255, 32},
             {"contiguous", "int", 0, 1, 1},
             {"mode", "int", 0, 3, 0}}},
        {"select_feather", "Feather/soften selection edges by blurring the mask", "Selection",
            {{"radius", "float", 0, 250, 5}}},
        {"select_grow", "Expand selection by N pixels", "Selection",
            {{"pixels", "int", 1, 250, 5}}},
        {"select_shrink", "Contract selection by N pixels", "Selection",
            {{"pixels", "int", 1, 250, 5}}},
        {"select_border", "Create a hollow border selection around current selection", "Selection",
            {{"pixels", "int", 1, 250, 5}}},
        {"select_smooth", "Smooth selection edges using morphological close+open", "Selection",
            {{"radius", "float", 0, 50, 3}}},
        {"select_save_channel", "Save current selection as an alpha channel", "Selection",
            {{"name", "string", 0, 0, 0}}},
        {"select_load_channel", "Load an alpha channel as selection", "Selection",
            {{"index", "int", 0, 100, 0}, {"mode", "int", 0, 3, 0}}},
        {"refine_edge", "Open Refine Edge dialog to refine selection edges", "Selection", {}},
        {"crop_to_selection", "Crop document to selection bounds", "Selection", {}},

        {"set_clone_source", "Set Clone Stamp source point in document/canvas coordinates", "Paint",
            {{"x", "float", -100000, 100000, 0},
             {"y", "float", -100000, 100000, 0}}},
        {"set_clone_sample_mode", "Set Clone Stamp sample mode (0=Current Layer, 1=Current & Below, 2=All Layers)", "Paint",
            {{"mode", "int", 0, 2, 1}}},
        {"set_clone_aligned", "Set Clone Stamp aligned mode (1=aligned, 0=non-aligned)", "Paint",
            {{"aligned", "int", 0, 1, 1}}},
        {"begin_clone_stroke", "Begin a Clone Stamp stroke in document/canvas coordinates", "Paint",
            {{"x", "float", -100000, 100000, 0},
             {"y", "float", -100000, 100000, 0}}},
        {"update_clone_stroke", "Continue a Clone Stamp stroke in document/canvas coordinates", "Paint",
            {{"x", "float", -100000, 100000, 0},
             {"y", "float", -100000, 100000, 0}}},
        {"end_clone_stroke", "End the active Clone Stamp stroke", "Paint", {}},

        {"set_healing_source", "Set Healing Brush source point in document/canvas coordinates", "Paint",
            {{"x", "float", -100000, 100000, 0},
             {"y", "float", -100000, 100000, 0}}},
        {"set_healing_sample_mode", "Set Healing Brush sample mode (0=Current Layer, 1=Current & Below, 2=All Layers)", "Paint",
            {{"mode", "int", 0, 2, 1}}},
        {"set_healing_aligned", "Set Healing Brush aligned mode (1=aligned, 0=non-aligned)", "Paint",
            {{"aligned", "int", 0, 1, 1}}},
        {"set_healing_diffusion", "Set Healing Brush diffusion/blend amount (0=preserve source detail, 1=blend into destination)", "Paint",
            {{"diffusion", "float", 0, 1, 0.5}}},
        {"begin_healing_stroke", "Begin a Healing Brush stroke in document/canvas coordinates", "Paint",
            {{"x", "float", -100000, 100000, 0},
             {"y", "float", -100000, 100000, 0}}},
        {"update_healing_stroke", "Continue a Healing Brush stroke in document/canvas coordinates", "Paint",
            {{"x", "float", -100000, 100000, 0},
             {"y", "float", -100000, 100000, 0}}},
        {"end_healing_stroke", "End the active Healing Brush stroke", "Paint", {}},

        {"layer_mask_add", "Add a white (reveal all) layer mask", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"layer_mask_remove", "Remove layer mask without applying", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"layer_mask_apply", "Apply layer mask (destructive)", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"layer_mask_toggle", "Toggle layer mask on/off", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"layer_mask_disable", "Disable layer mask", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"layer_mask_enable", "Enable layer mask", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"mask_invert", "Invert layer mask colors", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"mask_density", "Set layer mask density (0=fully transparent, 1=fully opaque)", "Layer",
            {{"index", "int", -1, 100, -1},
             {"density", "float", 0, 1, 1}}},
        {"mask_feather", "Blur/soften layer mask edges", "Layer",
            {{"index", "int", -1, 100, -1},
             {"radius", "float", 0, 250, 5}}},
        {"selection_to_mask", "Create a layer mask from the current selection", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"mask_copy", "Copy mask from active layer to clipboard", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"mask_paste", "Paste copied mask onto active layer", "Layer",
            {{"index", "int", -1, 100, -1}}},
        {"mask_clear", "Reset layer mask to all-white (reveal all)", "Layer",
            {{"index", "int", -1, 100, -1}}},

        {"create_text", "Create a new text layer", "Text",
            {{"text", "string", 0, 0, 0}, {"x", "float", -1, 1, 0},
             {"y", "float", -1, 1, 0}, {"width", "float", 0, 10000, 0},
             {"height", "float", 0, 10000, 0},
             {"size", "int", 8, 400, 32}}},
        {"edit_text", "Edit text content on a text layer", "Text",
            {{"index", "int", 0, 100, 0}, {"text", "string", 0, 0, 0}}},
        {"set_text_style", "Apply style to text layer", "Text",
            {{"index", "int", 0, 100, 0}, {"start", "int", 0, 10000, 0},
             {"end", "int", 0, 10000, 0},
             {"size", "int", 8, 400, 32},
             {"bold", "int", 0, 1, 0}, {"italic", "int", 0, 1, 0}}},
    };
}

const std::vector<ToolDefinition>& ToolCatalog::allTools()
{
    return s_catalog;
}

const ToolDefinition* ToolCatalog::findTool(const std::string& name)
{
    for (auto& t : s_catalog) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

std::string ToolCatalog::toJsonSchema()
{
    std::ostringstream os;
    os << "{\n  \"tools\": [\n";
    for (size_t i = 0; i < s_catalog.size(); ++i) {
        auto& t = s_catalog[i];
        os << "    {\n";
        os << "      \"name\": \"" << t.name << "\",\n";
        os << "      \"description\": \"" << t.description << "\",\n";
        os << "      \"category\": \"" << t.category << "\",\n";
        os << "      \"parameters\": [\n";
        for (size_t j = 0; j < t.params.size(); ++j) {
            auto& p = t.params[j];
            os << "        {\"name\":\"" << p.name << "\",\"type\":\"" << p.type
               << "\",\"min\":" << p.minVal << ",\"max\":" << p.maxVal
               << ",\"default\":" << p.defaultVal << "}";
            if (j < t.params.size() - 1) os << ",";
            os << "\n";
        }
        os << "      ]\n";
        os << "    }";
        if (i < s_catalog.size() - 1) os << ",";
        os << "\n";
    }
    os << "  ]\n}\n";
    return os.str();
}

std::string ToolCatalog::systemPrompt()
{
    std::ostringstream os;

    os << systemPrompt({});
    return os.str();
}

std::string ToolCatalog::systemPrompt(const std::vector<std::string>& enabledTools)
{
    auto& all = allTools();

    std::vector<const ToolDefinition*> filtered;
    if (enabledTools.empty()) {
        for (auto& t : all)
            filtered.push_back(&t);
    } else {
        for (auto& t : all) {
            if (std::find(enabledTools.begin(), enabledTools.end(), t.name) != enabledTools.end())
                filtered.push_back(&t);
        }
    }

    std::ostringstream os;

    os << "You are an AI image editing assistant. You have access to tools you can call.\n\n";
    os << "RULES:\n";
    os << "- Respond to the user naturally in the same language they use.\n";
    os << "- You can call tools by outputting a JSON array like:\n";
    os << "  [{\"tool\":\"tool_name\",\"params\":{\"param1\":value}}]\n";
    os << "- You can call multiple tools in a single response.\n";
    os << "- After calling tools, wait for results and then summarize what was done.\n";
    os << "- If the user's request doesn't match any tool, explain why politely.\n\n";
    os << "Available Tools:\n";

    std::string prevCategory;
    for (auto* tp : filtered) {
        auto& t = *tp;
        if (t.category != prevCategory) {
            os << "\n[" << t.category << "]\n";
            prevCategory = t.category;
        }
        os << "- " << t.name;
        if (!t.params.empty()) {
            os << "(";
            for (size_t j = 0; j < t.params.size(); ++j) {
                if (j > 0) os << ", ";
                os << t.params[j].name;
            }
            os << ")";
        } else {
            os << "()";
        }
        os << " -- " << t.description;
        if (!t.params.empty()) {
            os << " [";
            for (size_t j = 0; j < t.params.size(); ++j) {
                if (j > 0) os << ", ";
                os << t.params[j].name << ": ";
                if (t.params[j].type == "string")
                    os << "string";
                else
                    os << t.params[j].minVal << ".." << t.params[j].maxVal;
            }
            os << "]";
        }
        os << "\n";
    }

    os << "\nExamples:\n";
    os << "User: Make the image brighter\n";
    os << "Assistant: I'll increase the brightness for you.\n";
    os << "[{\"tool\":\"adjust_brightness\",\"params\":{\"value\":0.3}}]\n\n";
    os << "User: Convert to black and white then add a blur\n";
    os << "Assistant: Sure, I'll apply grayscale and then blur.\n";
    os << "[{\"tool\":\"grayscale\",\"params\":{}},{\"tool\":\"gaussian_blur\",\"params\":{\"radius\":3}}]\n";

    return os.str();
}
