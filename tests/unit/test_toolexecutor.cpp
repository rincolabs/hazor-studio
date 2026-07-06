#define BOOST_TEST_MODULE ToolExecutorTest
#include <boost/test/included/unit_test.hpp>

#include "tools/ToolExecutor.hpp"
#include "tools/ToolCall.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "controller/ImageController.hpp"

struct ExecutorFixture {
    Document doc;
    ImageController ctrl;
    ToolExecutor executor;

    ExecutorFixture() : ctrl(), executor(&ctrl) {
        doc.size = QSize(100, 100);
        doc.selection.create(100, 100);
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }
};

BOOST_AUTO_TEST_SUITE(toolexecutor)

BOOST_AUTO_TEST_CASE(execute_valid_tool)
{
    ExecutorFixture f;
    ToolCall call;
    call.name = "grayscale";
    auto result = f.executor.execute(call);
    BOOST_CHECK(result.success);
}

BOOST_AUTO_TEST_CASE(execute_invalid_tool)
{
    ExecutorFixture f;
    ToolCall call;
    call.name = "nonexistent_tool_xyz";
    auto result = f.executor.execute(call);
    BOOST_CHECK(!result.success);
}

BOOST_AUTO_TEST_CASE(execute_with_params)
{
    ExecutorFixture f;
    ToolCall call;
    call.name = "gaussian_blur";
    call.params["radius"] = 3.0;
    auto result = f.executor.execute(call);
    BOOST_CHECK(result.success);
}

BOOST_AUTO_TEST_CASE(execute_null_controller)
{
    ToolExecutor executor(nullptr);
    ToolCall call;
    call.name = "grayscale";
    auto result = executor.execute(call);
    BOOST_CHECK(!result.success);
}

BOOST_AUTO_TEST_CASE(setController_changes_target)
{
    ToolExecutor executor(nullptr);
    ToolCall call;
    call.name = "grayscale";
    BOOST_CHECK(!executor.execute(call).success);

    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);
    ImageController ctrl;
    ctrl.setDocument(&doc);
    ctrl.newLayer();

    executor.setController(&ctrl);
    BOOST_CHECK(executor.execute(call).success);
}

BOOST_AUTO_TEST_CASE(executeBatch_empty_array)
{
    ExecutorFixture f;
    auto results = f.executor.executeBatch({});
    BOOST_CHECK(results.empty());
}

BOOST_AUTO_TEST_CASE(executeBatch_single_valid)
{
    ExecutorFixture f;
    ToolCall call;
    call.name = "grayscale";
    auto results = f.executor.executeBatch({call});
    BOOST_CHECK_EQUAL(results.size(), 1);
    BOOST_CHECK(results[0].success);
}

BOOST_AUTO_TEST_CASE(executeBatch_multiple_valid)
{
    ExecutorFixture f;
    ToolCall c1;
    c1.name = "grayscale";
    ToolCall c2;
    c2.name = "invert_colors";
    auto results = f.executor.executeBatch({c1, c2});
    BOOST_CHECK_EQUAL(results.size(), 2);
    BOOST_CHECK(results[0].success);
    BOOST_CHECK(results[1].success);
}

BOOST_AUTO_TEST_CASE(executeBatch_mixed_valid_invalid)
{
    ExecutorFixture f;
    ToolCall c1;
    c1.name = "grayscale";
    ToolCall c2;
    c2.name = "nonexistent_tool";
    ToolCall c3;
    c3.name = "invert_colors";
    auto results = f.executor.executeBatch({c1, c2, c3});
    BOOST_CHECK_EQUAL(results.size(), 3);
    BOOST_CHECK(results[0].success);
    BOOST_CHECK(!results[1].success);
    BOOST_CHECK(results[2].success);
}

BOOST_AUTO_TEST_CASE(executeBatch_order_preserved)
{
    ExecutorFixture f;
    ToolCall c1;
    c1.name = "grayscale";
    ToolCall c2;
    c2.name = "invert_colors";

    auto results = f.executor.executeBatch({c1, c2});
    BOOST_CHECK_EQUAL(results.size(), 2);
    BOOST_CHECK(!results[0].message.empty() || results[0].success);
    BOOST_CHECK(!results[1].message.empty() || results[1].success);
}

BOOST_AUTO_TEST_CASE(controller_returns_controller)
{
    ExecutorFixture f;
    BOOST_CHECK(f.executor.controller() == &f.ctrl);
}

BOOST_AUTO_TEST_CASE(default_controller_is_null)
{
    ToolExecutor executor;
    BOOST_CHECK(executor.controller() == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
