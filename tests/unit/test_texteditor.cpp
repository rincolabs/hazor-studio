#define BOOST_TEST_MODULE TextEditorControllerTest
#include <boost/test/included/unit_test.hpp>

#include "text/TextEditorController.hpp"

#include <QCoreApplication>
#include <QColor>
#include <memory>

struct QCoreAppFixture {
    int argc = 1;
    char appName[16] = "test-texteditor";
    char* argv[2] = {appName, nullptr};
    std::unique_ptr<QCoreApplication> app;

    QCoreAppFixture()
    {
        if (!QCoreApplication::instance())
            app = std::make_unique<QCoreApplication>(argc, argv);
    }
};

BOOST_GLOBAL_FIXTURE(QCoreAppFixture);

static void checkSpanCoverage(const TextLayerData& data)
{
    int cursor = 0;
    for (const auto& span : data.spans) {
        BOOST_CHECK_EQUAL(span.start, cursor);
        BOOST_CHECK_GT(span.end, span.start);
        cursor = span.end;
    }
    BOOST_CHECK_EQUAL(cursor, data.text.size());
}

static TextSpan spanCovering(const TextLayerData& data, int pos)
{
    for (const auto& span : data.spans) {
        if (pos >= span.start && pos < span.end)
            return span;
    }
    return {};
}

BOOST_AUTO_TEST_CASE(insert_at_start_keeps_new_text_styleable)
{
    TextLayerData data;
    TextSpan initial;
    initial.start = 0;
    initial.end = 0;
    initial.fontSize = 32.0f;
    initial.color = Qt::black;
    data.spans.push_back(initial);

    TextEditorController editor;
    editor.beginEdit(&data);
    editor.insertText(QStringLiteral("palavra1"));
    editor.moveHome();
    editor.insertText(QStringLiteral("palavra2 "));

    BOOST_CHECK_EQUAL(data.text.toStdString(), "palavra2 palavra1");
    checkSpanCoverage(data);

    editor.setSelection(0, 8);
    editor.modifyCharStyle([](TextSpan& span) {
        span.bold = true;
        span.color = QColor(220, 10, 30);
    });

    checkSpanCoverage(data);

    const TextSpan firstWord = spanCovering(data, 0);
    BOOST_CHECK(firstWord.bold);
    BOOST_CHECK(firstWord.color == QColor(220, 10, 30));

    const TextSpan secondWord = spanCovering(data, 9);
    BOOST_CHECK(!secondWord.bold);
    BOOST_CHECK(secondWord.color == Qt::black);

    const TextSpan selectionStyle = editor.currentStyle();
    BOOST_CHECK(selectionStyle.bold);
    BOOST_CHECK(selectionStyle.color == QColor(220, 10, 30));
}

BOOST_AUTO_TEST_CASE(apply_to_range_materializes_missing_span_coverage)
{
    TextLayerData data;
    data.text = QStringLiteral("palavra2 palavra1");

    TextSpan original;
    original.start = 9;
    original.end = data.text.size();
    original.color = Qt::black;
    data.spans.push_back(original);

    TextEditorController editor;
    editor.beginEdit(&data);
    checkSpanCoverage(data);

    editor.setSelection(0, 8);
    editor.modifyCharStyle([](TextSpan& span) {
        span.italic = true;
        span.color = Qt::blue;
    });

    checkSpanCoverage(data);

    const TextSpan firstWord = spanCovering(data, 0);
    BOOST_CHECK(firstWord.italic);
    BOOST_CHECK(firstWord.color == Qt::blue);

    const TextSpan secondWord = spanCovering(data, 9);
    BOOST_CHECK(!secondWord.italic);
    BOOST_CHECK(secondWord.color == Qt::black);
}
