#define BOOST_TEST_MODULE GradientFeatureTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "controller/ImageController.hpp"
#include "gradient/GradientPresetManager.hpp"
#include "gradient/GradientRenderer.hpp"
#include "gradient/GradientTypes.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QTemporaryDir>

struct QtAppFixture {
    QtAppFixture() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = { const_cast<char*>("test-gradient") };
            static QApplication app(argc, argv);
        }
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

static GradientDefinition blackToWhite()
{
    GradientDefinition definition;
    definition.name = QStringLiteral("Black to White");
    definition.colorStops = {
        {Qt::black, 0.0, 0.5},
        {Qt::white, 1.0, 0.5},
    };
    definition.opacityStops = {
        {1.0, 0.0, 0.5},
        {1.0, 1.0, 0.5},
    };
    definition.smoothness = 0.0;
    definition.dither = false;
    definition.normalize();
    return definition;
}

struct GradientFixture {
    Document doc;
    ImageController ctrl;

    GradientFixture()
    {
        doc.size = QSize(8, 4);
        doc.selection.create(8, 4);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
        doc.activeLayer()->cpuImage.fill(QColor(100, 100, 100, 255));
    }
};

BOOST_AUTO_TEST_CASE(gradient_definition_json_roundtrip)
{
    GradientDefinition definition = blackToWhite();
    definition.kind = GradientKind::Diamond;
    definition.reverse = true;
    definition.interpolation = GradientInterpolationMethod::Classic;

    GradientDefinition copy = GradientDefinition::fromJson(definition.toJson());
    BOOST_CHECK_EQUAL(copy.name.toStdString(), "Black to White");
    BOOST_CHECK(copy.kind == GradientKind::Diamond);
    BOOST_CHECK(copy.reverse);
    BOOST_CHECK(copy.interpolation == GradientInterpolationMethod::Classic);
    BOOST_CHECK_EQUAL(copy.colorStops.size(), 2);
    BOOST_CHECK_EQUAL(copy.opacityStops.size(), 2);
}

BOOST_AUTO_TEST_CASE(gradient_normalize_preserves_endpoint_locations)
{
    GradientDefinition definition;
    definition.colorStops = {
        {QColor(20, 40, 60), 0.25, 0.5},
        {QColor(220, 180, 120), 0.75, 0.5},
    };
    definition.opacityStops = {
        {0.25, 0.20, 0.5},
        {0.85, 0.80, 0.5},
    };

    definition.normalize();

    BOOST_CHECK_CLOSE(definition.colorStops[0].position, 0.25, 0.001);
    BOOST_CHECK_CLOSE(definition.colorStops[1].position, 0.75, 0.001);
    BOOST_CHECK_CLOSE(definition.opacityStops[0].position, 0.20, 0.001);
    BOOST_CHECK_CLOSE(definition.opacityStops[1].position, 0.80, 0.001);

    BOOST_CHECK_EQUAL(GradientRenderer::sampleGradientAt(definition, 0.0).rgb(),
                      QColor(20, 40, 60).rgb());
    BOOST_CHECK_EQUAL(GradientRenderer::sampleGradientAt(definition, 1.0).rgb(),
                      QColor(220, 180, 120).rgb());
}

BOOST_AUTO_TEST_CASE(gradient_presets_save_and_load_hsgp)
{
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("custom-gradient.hsgp"));

    GradientPresetManager writer;
    BOOST_CHECK(writer.saveToFile(path));

    GradientPresetManager reader;
    BOOST_CHECK(reader.loadFromFile(path));

    bool found = false;
    for (const auto& preset : reader.presets()) {
        if (preset.name == QLatin1String("Black to White")) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
}

BOOST_AUTO_TEST_CASE(renderer_linear_gradient_reaches_endpoints)
{
    GradientRenderRequest request;
    request.definition = blackToWhite();
    request.targetSize = QSize(8, 1);
    request.startPoint = QPointF(0.5, 0.5);
    request.endPoint = QPointF(7.5, 0.5);

    const QImage image = GradientRenderer::renderGradientToImage(request);
    BOOST_REQUIRE(!image.isNull());
    BOOST_CHECK_LT(image.pixelColor(0, 0).red(), 20);
    BOOST_CHECK_GT(image.pixelColor(7, 0).red(), 235);
}

BOOST_AUTO_TEST_CASE(renderer_dither_preserves_exact_stop_colors)
{
    GradientDefinition definition;
    definition.colorStops = {
        {QColor(12, 34, 56), 0.0, 0.5},
        {QColor(222, 111, 44), 0.5, 0.5},
        {QColor(78, 90, 123), 1.0, 0.5},
    };
    definition.opacityStops = {
        {1.0, 0.0, 0.5},
        {1.0, 1.0, 0.5},
    };
    definition.dither = true;
    definition.smoothness = 0.0;
    definition.normalize();

    GradientRenderRequest request;
    request.definition = definition;
    request.targetSize = QSize(3, 1);
    request.startPoint = QPointF(0.5, 0.5);
    request.endPoint = QPointF(2.5, 0.5);

    const QImage image = GradientRenderer::renderGradientToImage(request);
    BOOST_REQUIRE(!image.isNull());
    BOOST_CHECK_EQUAL(image.pixelColor(0, 0).rgba(), QColor(12, 34, 56, 255).rgba());
    BOOST_CHECK_EQUAL(image.pixelColor(1, 0).rgba(), QColor(222, 111, 44, 255).rgba());
    BOOST_CHECK_EQUAL(image.pixelColor(2, 0).rgba(), QColor(78, 90, 123, 255).rgba());
}

BOOST_AUTO_TEST_CASE(controller_apply_gradient_is_undoable)
{
    GradientFixture f;
    const QImage before = f.doc.activeLayer()->cpuImage.copy();

    GradientApplication application;
    application.definition = blackToWhite();
    application.startPoint = QPointF(0.5, 0.5);
    application.endPoint = QPointF(7.5, 0.5);
    application.opacity = 1.0;

    BOOST_CHECK(f.ctrl.applyGradient(application));
    BOOST_CHECK_LT(f.doc.activeLayer()->cpuImage.pixelColor(0, 0).red(), 20);
    BOOST_CHECK_GT(f.doc.activeLayer()->cpuImage.pixelColor(7, 0).red(), 235);

    f.ctrl.history().undo();
    BOOST_CHECK(f.doc.activeLayer()->cpuImage == before);
}

BOOST_AUTO_TEST_CASE(controller_apply_gradient_respects_selection)
{
    GradientFixture f;
    f.doc.selection.setRect(QRectF(0, 0, 4, 4), SelectMode::Replace);
    f.doc.selection.setActive(true);

    GradientApplication application;
    application.definition = blackToWhite();
    application.startPoint = QPointF(0.5, 0.5);
    application.endPoint = QPointF(7.5, 0.5);
    application.opacity = 1.0;

    BOOST_CHECK(f.ctrl.applyGradient(application));
    BOOST_CHECK_NE(f.doc.activeLayer()->cpuImage.pixelColor(1, 1).rgba(),
                   QColor(100, 100, 100, 255).rgba());
    BOOST_CHECK_EQUAL(f.doc.activeLayer()->cpuImage.pixelColor(6, 1).rgba(),
                      QColor(100, 100, 100, 255).rgba());
}
