#define BOOST_TEST_MODULE ToolCatalogTest
#include <boost/test/included/unit_test.hpp>

#include "tools/ToolCatalog.hpp"
#include "tools/ToolCall.hpp"

BOOST_AUTO_TEST_SUITE(toolcatalog)

BOOST_AUTO_TEST_CASE(all_tools_count)
{
    auto& tools = ToolCatalog::allTools();
    BOOST_CHECK_EQUAL(tools.size(), 72);
}

BOOST_AUTO_TEST_CASE(each_tool_has_name_and_category)
{
    for (const auto& t : ToolCatalog::allTools()) {
        BOOST_CHECK(!t.name.empty());
        BOOST_CHECK(!t.category.empty());
        BOOST_CHECK(!t.description.empty());
    }
}

BOOST_AUTO_TEST_CASE(find_tool_by_name)
{
    auto* found = ToolCatalog::findTool("gaussian_blur");
    BOOST_REQUIRE(found != nullptr);
    BOOST_CHECK_EQUAL(found->name, "gaussian_blur");
    BOOST_CHECK_EQUAL(found->category, "Filter");
}

BOOST_AUTO_TEST_CASE(find_tool_returns_null_for_unknown)
{
    auto* found = ToolCatalog::findTool("nonexistent_tool");
    BOOST_CHECK(found == nullptr);
}

BOOST_AUTO_TEST_CASE(tool_has_params_with_ranges)
{
    auto* crop = ToolCatalog::findTool("crop");
    BOOST_REQUIRE(crop != nullptr);
    BOOST_REQUIRE(crop->params.size() >= 4);

    auto checkParam = [&](const std::string& name, double minV, double maxV, double defV) {
        for (const auto& p : crop->params) {
            if (p.name == name) {
                BOOST_CHECK_EQUAL(p.minVal, minV);
                BOOST_CHECK_EQUAL(p.maxVal, maxV);
                BOOST_CHECK_EQUAL(p.defaultVal, defV);
                return;
            }
        }
        BOOST_FAIL("Parameter '" + name + "' not found in crop");
    };

    checkParam("x", 0, 10000, 0);
    checkParam("y", 0, 10000, 0);
    checkParam("width", 1, 10000, 512);
    checkParam("height", 1, 10000, 512);
}

BOOST_AUTO_TEST_CASE(blend_mode_tool_exists)
{
    auto* blend = ToolCatalog::findTool("set_layer_blend_mode");
    BOOST_REQUIRE(blend != nullptr);
    BOOST_CHECK_EQUAL(blend->category, "Layer");
    BOOST_REQUIRE(blend->params.size() >= 2);

    bool hasMode = false;
    for (const auto& p : blend->params) {
        if (p.name == "mode") {
            hasMode = true;
            BOOST_CHECK_EQUAL(p.minVal, 0);
            BOOST_CHECK_EQUAL(p.maxVal, 15);
            break;
        }
    }
    BOOST_CHECK(hasMode);
}

BOOST_AUTO_TEST_CASE(no_params_has_correct_defaults)
{
    auto* gs = ToolCatalog::findTool("grayscale");
    BOOST_REQUIRE(gs != nullptr);
    BOOST_CHECK(gs->params.empty());
}

BOOST_AUTO_TEST_CASE(system_prompt_not_empty)
{
    std::string prompt = ToolCatalog::systemPrompt();
    BOOST_CHECK(!prompt.empty());
}

BOOST_AUTO_TEST_CASE(system_prompt_with_filters)
{
    std::vector<std::string> enabled = {"grayscale", "invert_colors"};
    std::string prompt = ToolCatalog::systemPrompt(enabled);
    BOOST_CHECK(!prompt.empty());
    BOOST_CHECK(prompt.find("grayscale") != std::string::npos);
    BOOST_CHECK(prompt.find("invert_colors") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(to_json_schema_valid)
{
    std::string schema = ToolCatalog::toJsonSchema();
    BOOST_CHECK(!schema.empty());
    BOOST_CHECK(schema.find("new_document") != std::string::npos);
    BOOST_CHECK(schema.find("reset_view") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(selection_tools_exist)
{
    BOOST_CHECK(ToolCatalog::findTool("select_all") != nullptr);
    BOOST_CHECK(ToolCatalog::findTool("deselect") != nullptr);
    BOOST_CHECK(ToolCatalog::findTool("select_invert") != nullptr);
    BOOST_CHECK(ToolCatalog::findTool("select_rect") != nullptr);
    BOOST_CHECK(ToolCatalog::findTool("crop_to_selection") != nullptr);
}

BOOST_AUTO_TEST_CASE(copy_paste_tools_exist)
{
    auto* copy = ToolCatalog::findTool("copy");
    BOOST_REQUIRE(copy != nullptr);
    BOOST_CHECK_EQUAL(copy->category, "Layer");
    BOOST_CHECK(copy->params.empty());

    auto* paste = ToolCatalog::findTool("paste");
    BOOST_REQUIRE(paste != nullptr);
    BOOST_CHECK_EQUAL(paste->category, "Layer");
    BOOST_REQUIRE(paste->params.size() >= 1);
    BOOST_CHECK_EQUAL(paste->params[0].name, "offset");
}

BOOST_AUTO_TEST_SUITE_END()
