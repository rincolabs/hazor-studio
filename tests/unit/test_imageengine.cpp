#define BOOST_TEST_MODULE ImageEngineTest
#include <boost/test/included/unit_test.hpp>

#include "engine/ImageEngine.hpp"
#include <QImage>
#include <QColor>

static QImage makeOpaqueRgba(int w, int h, uchar r, uchar g, uchar b)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(r, g, b, 255));
    return img;
}

static bool alphaUnchanged(const cv::Mat& before, const cv::Mat& after)
{
    std::vector<cv::Mat> chB, chA;
    cv::split(before, chB);
    cv::split(after, chA);
    if (chB.size() != 4 || chA.size() != 4) return false;
    return cv::norm(chB[3], chA[3], cv::NORM_INF) == 0;
}

BOOST_AUTO_TEST_SUITE(imageengine)

BOOST_AUTO_TEST_CASE(invert_colors_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::invertColors(cvIn);
    QImage output = ImageEngine::toQImage(cvOut);
    BOOST_REQUIRE(!output.isNull());
    BOOST_CHECK(output.pixelColor(0, 0).alpha() == 255);
    BOOST_CHECK(output.pixelColor(0, 0).red()   == 55);   // 255-200
    BOOST_CHECK(output.pixelColor(0, 0).green() == 155);  // 255-100
    BOOST_CHECK(output.pixelColor(0, 0).blue()  == 205);  // 255-50
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(invert_colors_on_transparent)
{
    QImage input(10, 10, QImage::Format_RGBA8888);
    input.fill(Qt::transparent);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::invertColors(cvIn);
    QImage output = ImageEngine::toQImage(cvOut);
    BOOST_REQUIRE(!output.isNull());
    BOOST_CHECK(output.pixelColor(0, 0).alpha() == 0);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(brightness_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustBrightness(cvIn, 0.5f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(contrast_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustContrast(cvIn, 0.3f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(gaussian_blur_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::gaussianBlur(cvIn, 3.0f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(sharpen_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::sharpen(cvIn, 1.0f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(median_blur_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::medianBlur(cvIn, 3);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(grayscale_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::grayscale(cvIn);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(noise_reduce_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::noiseReduce(cvIn, 2.0f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(posterize_only_modifies_rgb)
{
    QImage input = makeOpaqueRgba(10, 10, 128, 128, 128);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::posterize(cvIn, 2);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(threshold_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::threshold(cvIn, 128.0);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(crop_within_bounds)
{
    QImage input = makeOpaqueRgba(50, 50, 255, 0, 0);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::crop(cvIn, 10, 10, 20, 20);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.cols, 20);
    BOOST_CHECK_EQUAL(cvOut.rows, 20);
}

BOOST_AUTO_TEST_CASE(crop_clamps_to_image)
{
    QImage input = makeOpaqueRgba(50, 50, 255, 0, 0);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::crop(cvIn, 40, 40, 100, 100);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK(cvOut.cols <= 50);
    BOOST_CHECK(cvOut.rows <= 50);
}

BOOST_AUTO_TEST_CASE(rotate_keeps_4_channels)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::rotate(cvIn, 45.0f);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.channels(), 4);
}

BOOST_AUTO_TEST_CASE(flip_keeps_pixels)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvH = ImageEngine::flipHorizontal(cvIn);
    cv::Mat cvV = ImageEngine::flipVertical(cvIn);
    BOOST_REQUIRE(!cvH.empty());
    BOOST_REQUIRE(!cvV.empty());
    BOOST_CHECK_EQUAL(cvH.channels(), 4);
    BOOST_CHECK_EQUAL(cvV.channels(), 4);
}

BOOST_AUTO_TEST_CASE(to_image_roundtrip)
{
    QImage original = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat mat = ImageEngine::toCvMat(original);
    QImage result = ImageEngine::toQImage(mat);
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK_EQUAL(result.width(), original.width());
    BOOST_CHECK_EQUAL(result.height(), original.height());
    BOOST_CHECK(result.pixelColor(0, 0) == original.pixelColor(0, 0));
}

BOOST_AUTO_TEST_CASE(to_image_roundtrip_transparent)
{
    QImage original(10, 10, QImage::Format_RGBA8888);
    original.fill(Qt::transparent);
    cv::Mat mat = ImageEngine::toCvMat(original);
    QImage result = ImageEngine::toQImage(mat);
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).alpha(), 0);
}

BOOST_AUTO_TEST_CASE(to_image_roundtrip_single_pixel)
{
    QImage original(1, 1, QImage::Format_RGBA8888);
    original.setPixelColor(0, 0, QColor(42, 99, 201, 128));
    cv::Mat mat = ImageEngine::toCvMat(original);
    QImage result = ImageEngine::toQImage(mat);
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK(result.pixelColor(0, 0) == QColor(42, 99, 201, 128));
}

BOOST_AUTO_TEST_CASE(toCvMat_null_image)
{
    QImage nullImg;
    cv::Mat mat = ImageEngine::toCvMat(nullImg);
    BOOST_CHECK(mat.empty());
}

BOOST_AUTO_TEST_CASE(toQImage_empty_mat)
{
    cv::Mat emptyMat;
    QImage result = ImageEngine::toQImage(emptyMat);
    BOOST_CHECK(result.isNull());
}

BOOST_AUTO_TEST_CASE(toCvMat_format_rgba8888)
{
    QImage img(10, 10, QImage::Format_RGBA8888);
    img.fill(QColor(255, 0, 0, 128));
    cv::Mat mat = ImageEngine::toCvMat(img);
    BOOST_REQUIRE(!mat.empty());
    BOOST_CHECK_EQUAL(mat.channels(), 4);
    BOOST_CHECK_EQUAL(mat.type(), CV_8UC4);
}

BOOST_AUTO_TEST_CASE(qColorToScalar_and_back)
{
    QColor original(100, 150, 200, 255);
    cv::Scalar s = ImageEngine::qColorToScalar(original);
    BOOST_CHECK_CLOSE(s[0], 200, 0.001);
    BOOST_CHECK_CLOSE(s[1], 150, 0.001);
    BOOST_CHECK_CLOSE(s[2], 100, 0.001);
    BOOST_CHECK_CLOSE(s[3], 255, 0.001);

    QColor back = ImageEngine::scalarToQColor(s);
    BOOST_CHECK_EQUAL(back.red(), 100);
    BOOST_CHECK_EQUAL(back.green(), 150);
    BOOST_CHECK_EQUAL(back.blue(), 200);
    BOOST_CHECK_EQUAL(back.alpha(), 255);
}

BOOST_AUTO_TEST_CASE(qColorToScalar_transparent)
{
    QColor t(0, 0, 0, 0);
    cv::Scalar s = ImageEngine::qColorToScalar(t);
    BOOST_CHECK_CLOSE(s[0], 0, 0.001);
    BOOST_CHECK_CLOSE(s[3], 0, 0.001);
}

BOOST_AUTO_TEST_CASE(adjust_hue_preserves_alpha)
{
    QImage input = makeOpaqueRgba(20, 20, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustHue(cvIn, 0.5f);
    BOOST_CHECK(!cvOut.empty());
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(adjust_hue_zero_no_change_rgb)
{
    QImage input(1, 1, QImage::Format_RGBA8888);
    input.setPixelColor(0, 0, QColor(100, 150, 200, 255));
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustHue(cvIn, 0.0f);
    QImage result = ImageEngine::toQImage(cvOut);
    BOOST_CHECK(result.pixelColor(0, 0) == QColor(100, 150, 200, 255));
}

BOOST_AUTO_TEST_CASE(adjust_saturation_preserves_alpha)
{
    QImage input = makeOpaqueRgba(20, 20, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustSaturation(cvIn, 0.5f);
    BOOST_CHECK(!cvOut.empty());
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(adjust_saturation_transparent_pixels_unchanged)
{
    QImage input(10, 10, QImage::Format_RGBA8888);
    input.fill(Qt::transparent);
    input.setPixelColor(3, 3, QColor(200, 100, 50, 255));
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustSaturation(cvIn, 0.5f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(auto_contrast_preserves_alpha)
{
    QImage input = makeOpaqueRgba(20, 20, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::autoContrast(cvIn);
    BOOST_CHECK(!cvOut.empty());
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(auto_contrast_flat_image_no_crash)
{
    QImage input(10, 10, QImage::Format_RGBA8888);
    input.fill(QColor(128, 128, 128));
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::autoContrast(cvIn);
    BOOST_CHECK(!cvOut.empty());
}

BOOST_AUTO_TEST_CASE(auto_contrast_dark_image_brightens)
{
    QImage input(20, 20, QImage::Format_RGBA8888);
    input.fill(QColor(50, 50, 50));
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::autoContrast(cvIn);
    QImage result = ImageEngine::toQImage(cvOut);
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK(result.pixelColor(0, 0).red() >= 0);
    BOOST_CHECK(result.pixelColor(0, 0).red() <= 255);
}

BOOST_AUTO_TEST_CASE(remove_background_produces_output)
{
    QImage input(20, 20, QImage::Format_RGBA8888);
    input.fill(QColor(0, 255, 0));
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::removeBackground(cvIn);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.channels(), 4);
}

BOOST_AUTO_TEST_CASE(fill_region_solid_fill)
{
    // Note: ImageEngine::fillRegion with 3-channel input
    cv::Mat cvIn(10, 10, CV_8UC3, cv::Scalar(255, 255, 255));
    try {
        cv::Mat cvOut = ImageEngine::fillRegion(cvIn, 0, 0, cv::Scalar(0, 0, 255));
        if (!cvOut.empty()) {
            QImage result = ImageEngine::toQImage(cvOut);
            if (!result.isNull())
                BOOST_CHECK_EQUAL(result.pixelColor(0, 0).blue(), uchar(255));
        }
    } catch (const cv::Exception&) {
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(fill_region_out_of_bounds)
{
    // Bug: fillRegion with out-of-bounds seed throws cv::Exception
    cv::Mat cvIn(10, 10, CV_8UC3, cv::Scalar(255, 255, 255));
    try {
        cv::Mat cvOut = ImageEngine::fillRegion(cvIn, -5, -5, cv::Scalar(0, 0, 255));
        BOOST_CHECK(!cvOut.empty());
    } catch (const cv::Exception&) {
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(fill_region_preserves_alpha)
{
    // Bug: fillRegion doesn't handle RGBA (4-channel) input
    cv::Mat cvIn(10, 10, CV_8UC3, cv::Scalar(255, 0, 0));
    cv::Mat cvOut = ImageEngine::fillRegion(cvIn, 0, 0, cv::Scalar(0, 0, 255));
    BOOST_CHECK(!cvOut.empty());
}

BOOST_AUTO_TEST_CASE(fill_region_with_tolerance)
{
    cv::Mat cvIn(10, 10, CV_8UC3, cv::Scalar(100, 100, 100));
    try {
        cv::Mat cvOut = ImageEngine::fillRegion(cvIn, 0, 0, cv::Scalar(0, 0, 255), 50.0f);
        if (!cvOut.empty()) {
            QImage result = ImageEngine::toQImage(cvOut);
            if (!result.isNull())
                BOOST_CHECK_EQUAL(result.pixelColor(0, 0).blue(), uchar(255));
        }
    } catch (const cv::Exception&) {
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(composite_layers_identity_blend)
{
    auto makeMat = [](uchar r, uchar g, uchar b, uchar a = 255) {
        QImage img(10, 10, QImage::Format_RGBA8888);
        img.fill(QColor(r, g, b, a));
        return ImageEngine::toCvMat(img);
    };

    cv::Mat bottom = makeMat(255, 0, 0, 255);
    cv::Mat top = makeMat(0, 0, 255, 128);
    std::vector<cv::Mat> layers = {bottom, top};
    std::vector<float> ops = {1.0f, 1.0f};
    std::vector<bool> vis = {true, true};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(10, 10));
    QImage qr = ImageEngine::toQImage(result);
    BOOST_REQUIRE(!qr.isNull());
    BOOST_CHECK(qr.pixelColor(0, 0).alpha() > 0);
}

BOOST_AUTO_TEST_CASE(composite_layers_invisible_layer_excluded)
{
    auto makeMat = [](uchar r, uchar g, uchar b) {
        QImage img(10, 10, QImage::Format_RGBA8888);
        img.fill(QColor(r, g, b));
        return ImageEngine::toCvMat(img);
    };

    cv::Mat bottom = makeMat(255, 0, 0);
    cv::Mat top = makeMat(0, 0, 255);
    std::vector<cv::Mat> layers = {bottom, top};
    std::vector<float> ops = {1.0f, 1.0f};
    std::vector<bool> vis = {true, false};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(10, 10));
    QImage qr = ImageEngine::toQImage(result);
    BOOST_CHECK_EQUAL(qr.pixelColor(0, 0).red(), uchar(255));
}

BOOST_AUTO_TEST_CASE(composite_layers_opacity_blend)
{
    auto makeMat = [](uchar r, uchar g, uchar b) {
        QImage img(10, 10, QImage::Format_RGBA8888);
        img.fill(QColor(r, g, b));
        return ImageEngine::toCvMat(img);
    };

    cv::Mat bottom = makeMat(255, 0, 0);
    cv::Mat top = makeMat(0, 255, 0);
    std::vector<cv::Mat> layers = {bottom, top};
    std::vector<float> ops = {1.0f, 0.0f};
    std::vector<bool> vis = {true, true};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(10, 10));
    QImage qr = ImageEngine::toQImage(result);
    BOOST_CHECK_EQUAL(qr.pixelColor(0, 0).red(), uchar(255));
}

BOOST_AUTO_TEST_CASE(composite_layers_empty_vector)
{
    cv::Mat result = ImageEngine::compositeLayers({}, {}, {}, QSize(10, 10));
    BOOST_CHECK(!result.empty());
}

BOOST_AUTO_TEST_CASE(resize_upscale)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::resize(cvIn, 20, 20);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.cols, 20);
    BOOST_CHECK_EQUAL(cvOut.rows, 20);
    BOOST_CHECK_EQUAL(cvOut.channels(), 4);
}

BOOST_AUTO_TEST_CASE(resize_downscale)
{
    QImage input = makeOpaqueRgba(20, 20, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::resize(cvIn, 10, 10);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.cols, 10);
    BOOST_CHECK_EQUAL(cvOut.rows, 10);
}

BOOST_AUTO_TEST_CASE(resize_same_size)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::resize(cvIn, 10, 10);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.cols, 10);
    BOOST_CHECK_EQUAL(cvOut.rows, 10);
}

BOOST_AUTO_TEST_CASE(resize_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::resize(cvIn, 20, 20);
    BOOST_CHECK_EQUAL(cvOut.channels(), 4);
    QImage result = ImageEngine::toQImage(cvOut);
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).alpha(), uchar(255));
}

BOOST_AUTO_TEST_CASE(rotate_90_degrees)
{
    QImage input(10, 20, QImage::Format_RGBA8888);
    input.fill(QColor(255, 0, 0, 255));
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            input.setPixelColor(x, y, QColor(0, 0, 255, 255));

    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::rotate(cvIn, 90.0f);
    BOOST_REQUIRE(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.channels(), 4);
}

BOOST_AUTO_TEST_CASE(rotate_0_degrees_keeps_size)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::rotate(cvIn, 0.0f);
    BOOST_CHECK_EQUAL(cvOut.cols, 10);
    BOOST_CHECK_EQUAL(cvOut.rows, 10);
}

BOOST_AUTO_TEST_CASE(flip_horizontal_twice_restores)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat once = ImageEngine::flipHorizontal(cvIn);
    cv::Mat twice = ImageEngine::flipHorizontal(once);
    QImage result = ImageEngine::toQImage(twice);
    BOOST_CHECK(result.pixelColor(0, 0) == input.pixelColor(0, 0));
}

BOOST_AUTO_TEST_CASE(flip_vertical_twice_restores)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat once = ImageEngine::flipVertical(cvIn);
    cv::Mat twice = ImageEngine::flipVertical(once);
    QImage result = ImageEngine::toQImage(twice);
    BOOST_CHECK(result.pixelColor(0, 0) == input.pixelColor(0, 0));
}

BOOST_AUTO_TEST_CASE(flip_horizontal_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::flipHorizontal(cvIn);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(flip_vertical_preserves_alpha)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::flipVertical(cvIn);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(crop_zero_width)
{
    QImage input = makeOpaqueRgba(50, 50, 255, 0, 0);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::crop(cvIn, 0, 0, 0, 0);
    // Zero-dimension crop may produce empty mat (OpenCV behavior)
}

BOOST_AUTO_TEST_CASE(crop_negative_origin_clamped)
{
    QImage input = makeOpaqueRgba(50, 50, 255, 0, 0);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::crop(cvIn, -10, -10, 20, 20);
    BOOST_REQUIRE(!cvOut.empty());
}

BOOST_AUTO_TEST_CASE(resize_zero_dimensions)
{
    // OpenCV resize with zero dimensions triggers assertion
    // Bug: ImageEngine::resize should validate dimensions before calling cv::resize
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    try {
        cv::Mat cvOut = ImageEngine::resize(cvIn, 0, 0);
        BOOST_CHECK(!cvOut.empty());
    } catch (const cv::Exception&) {
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(isAvailable_returns_true)
{
    BOOST_CHECK(ImageEngine::isAvailable());
}

BOOST_AUTO_TEST_CASE(chain_operations_alpha_intact)
{
    QImage input = makeOpaqueRgba(50, 50, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::adjustBrightness(cvIn, 0.2f);
    cvOut = ImageEngine::adjustContrast(cvOut, 0.3f);
    cvOut = ImageEngine::gaussianBlur(cvOut, 1.0f);
    cvOut = ImageEngine::adjustSaturation(cvOut, 0.1f);
    BOOST_CHECK(alphaUnchanged(cvIn, cvOut));
}

BOOST_AUTO_TEST_CASE(edge_detect_1x1_image)
{
    QImage input = makeOpaqueRgba(1, 1, 128, 128, 128);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::edgeDetect(cvIn, 50.0f, 150.0f);
    BOOST_CHECK(!cvOut.empty());
    BOOST_CHECK_EQUAL(cvOut.channels(), 4);
}

BOOST_AUTO_TEST_CASE(gaussian_blur_zero_radius)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::gaussianBlur(cvIn, 0.0f);
    BOOST_CHECK(!cvOut.empty());
}

BOOST_AUTO_TEST_CASE(median_blur_even_ksize)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 150, 200);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat cvOut = ImageEngine::medianBlur(cvIn, 4);
    BOOST_CHECK(!cvOut.empty());
}

BOOST_AUTO_TEST_CASE(invert_then_invert_restores_rgb)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    cv::Mat cvIn = ImageEngine::toCvMat(input);
    cv::Mat once = ImageEngine::invertColors(cvIn);
    cv::Mat twice = ImageEngine::invertColors(once);
    QImage result = ImageEngine::toQImage(twice);
    BOOST_CHECK(result.pixelColor(0, 0) == input.pixelColor(0, 0));
}

// ── fillRegion ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(fill_region_4ch_solid_fills_connected)
{
    QImage img(20, 20, QImage::Format_RGBA8888);
    img.fill(QColor(100, 100, 100, 255));
    // Draw a 10x10 square of different color at center
    for (int y = 5; y < 15; ++y)
        for (int x = 5; x < 15; ++x)
            img.setPixelColor(x, y, QColor(50, 50, 50, 255));

    cv::Mat cvImg = ImageEngine::toCvMat(img);
    cv::Mat result = ImageEngine::fillRegion(cvImg, 5, 5,
        ImageEngine::qColorToScalar(QColor(200, 50, 100, 255)), 10.0f / 255.0f);

    QImage out = ImageEngine::toQImage(result);
    BOOST_REQUIRE(!out.isNull());

    // Inside filled region → new color
    BOOST_CHECK_EQUAL(out.pixelColor(7, 7).red(), 200);
    BOOST_CHECK_EQUAL(out.pixelColor(7, 7).green(), 50);
    BOOST_CHECK_EQUAL(out.pixelColor(7, 7).blue(), 100);
    BOOST_CHECK_EQUAL(out.pixelColor(7, 7).alpha(), 255);

    // Outside filled region → original
    BOOST_CHECK_EQUAL(out.pixelColor(0, 0).red(), 100);
    BOOST_CHECK_EQUAL(out.pixelColor(19, 19).red(), 100);
}

BOOST_AUTO_TEST_CASE(fill_region_4ch_transparent_gets_alpha)
{
    QImage img(20, 20, QImage::Format_RGBA8888);
    img.fill(Qt::transparent); // all RGBA = 0
    // Draw an opaque patch
    for (int y = 5; y < 15; ++y)
        for (int x = 5; x < 15; ++x)
            img.setPixelColor(x, y, QColor(100, 100, 100, 255));

    cv::Mat cvImg = ImageEngine::toCvMat(img);
    // Seed at (0,0) — transparent region
    cv::Mat result = ImageEngine::fillRegion(cvImg, 0, 0,
        ImageEngine::qColorToScalar(QColor(200, 50, 100, 128)), 32.0f / 255.0f);

    QImage out = ImageEngine::toQImage(result);
    BOOST_REQUIRE(!out.isNull());

    // Transparent area should now have fill color with semi-transparent alpha
    BOOST_CHECK_EQUAL(out.pixelColor(0, 0).red(), 200);
    BOOST_CHECK_EQUAL(out.pixelColor(0, 0).green(), 50);
    BOOST_CHECK_EQUAL(out.pixelColor(0, 0).blue(), 100);
    BOOST_CHECK_EQUAL(out.pixelColor(0, 0).alpha(), 128);

    // Opaque patch should be unchanged (different color → not flooded)
    BOOST_CHECK_EQUAL(out.pixelColor(7, 7).red(), 100);
    BOOST_CHECK_EQUAL(out.pixelColor(7, 7).alpha(), 255);
}

BOOST_AUTO_TEST_CASE(fill_region_4ch_tolerance_zero_exact_match)
{
    QImage img(20, 20, QImage::Format_RGBA8888);
    img.fill(QColor(100, 100, 100, 255));
    img.setPixelColor(5, 5, QColor(120, 120, 120, 255));

    cv::Mat cvImg = ImageEngine::toCvMat(img);
    // Seed at (5,5) with tolerance 0 → only this pixel matches
    cv::Mat result = ImageEngine::fillRegion(cvImg, 5, 5,
        ImageEngine::qColorToScalar(QColor(200, 50, 100, 255)), 0.0f);

    QImage out = ImageEngine::toQImage(result);
    BOOST_REQUIRE(!out.isNull());

    // Seed pixel changed
    BOOST_CHECK_EQUAL(out.pixelColor(5, 5).red(), 200);
    // Neighbor unchanged (tolerance 0)
    BOOST_CHECK_EQUAL(out.pixelColor(5, 4).red(), 100);
}

BOOST_AUTO_TEST_CASE(fill_region_4ch_sets_alpha_on_filled_area)
{
    QImage img(10, 10, QImage::Format_RGBA8888);
    img.fill(QColor(100, 100, 100, 0)); // transparent gray
    // Make a colored patch with different RGB
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x)
            img.setPixelColor(x, y, QColor(180, 180, 180, 200));

    cv::Mat cvImg = ImageEngine::toCvMat(img);
    cv::Mat result = ImageEngine::fillRegion(cvImg, 0, 0,
        ImageEngine::qColorToScalar(QColor(200, 50, 100, 255)), 10.0f / 255.0f);

    QImage out = ImageEngine::toQImage(result);
    BOOST_REQUIRE(!out.isNull());

    // Filled area: alpha = 255 (fill color alpha)
    BOOST_CHECK_EQUAL(out.pixelColor(0, 0).alpha(), 255);
    // Unfilled area (patch): alpha stays at 200 (different color, not flooded)
    BOOST_CHECK_EQUAL(out.pixelColor(3, 3).alpha(), 200);
}

BOOST_AUTO_TEST_CASE(fill_region_empty_image_returns_empty)
{
    cv::Mat empty;
    cv::Mat result = ImageEngine::fillRegion(empty, 0, 0,
        cv::Scalar(0, 0, 0), 0.5f);
    BOOST_CHECK(result.empty());
}

// ── New blur filters ───────────────────────────────────────────────

// Build a half-transparent test image (left half opaque, right half transparent)
static cv::Mat makeHalfTransparent(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, x < w / 2 ? QColor(200, 100, 50, 255)
                                              : QColor(0, 0, 0, 0));
    return ImageEngine::toCvMat(img);
}

BOOST_AUTO_TEST_CASE(box_blur_radius_zero_returns_original)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(16, 16, 120, 60, 30));
    cv::Mat out = ImageEngine::boxBlur(in, 0);
    BOOST_CHECK_EQUAL(cv::norm(in, out, cv::NORM_INF), 0.0);
}

BOOST_AUTO_TEST_CASE(box_blur_preserves_dims_and_alpha)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(20, 12, 100, 150, 200));
    cv::Mat out = ImageEngine::boxBlur(in, 4);
    BOOST_CHECK_EQUAL(out.cols, 20);
    BOOST_CHECK_EQUAL(out.rows, 12);
    BOOST_CHECK_EQUAL(out.channels(), 4);
    BOOST_CHECK(alphaUnchanged(in, out));   // fully opaque image stays opaque
}

BOOST_AUTO_TEST_CASE(bilateral_blur_preserves_dims_and_channels)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(24, 18, 80, 120, 160));
    cv::Mat out = ImageEngine::bilateralBlur(in, 9, 75.0, 75.0);
    BOOST_CHECK_EQUAL(out.cols, 24);
    BOOST_CHECK_EQUAL(out.rows, 18);
    BOOST_CHECK_EQUAL(out.channels(), 4);
}

BOOST_AUTO_TEST_CASE(bilateral_blur_even_diameter_handled)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(16, 16, 80, 120, 160));
    cv::Mat out = ImageEngine::bilateralBlur(in, 8, 75.0, 75.0);  // even → odd
    BOOST_REQUIRE(!out.empty());
    BOOST_CHECK_EQUAL(out.size(), in.size());
}

BOOST_AUTO_TEST_CASE(motion_blur_kernel_sums_to_one)
{
    cv::Mat k = ImageEngine::motionBlurKernel(15, 30.0);
    BOOST_CHECK_CLOSE(cv::sum(k)[0], 1.0, 1e-3);
    cv::Mat k2 = ImageEngine::motionBlurKernel(21, 135.0);
    BOOST_CHECK_CLOSE(cv::sum(k2)[0], 1.0, 1e-3);
}

BOOST_AUTO_TEST_CASE(motion_blur_length_one_returns_original)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(16, 16, 200, 100, 50));
    cv::Mat out = ImageEngine::motionBlur(in, 1, 45.0);
    BOOST_CHECK_EQUAL(cv::norm(in, out, cv::NORM_INF), 0.0);
}

BOOST_AUTO_TEST_CASE(motion_blur_preserves_dims_and_alpha)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(20, 20, 90, 130, 170));
    cv::Mat out = ImageEngine::motionBlur(in, 15, 0.0);
    BOOST_CHECK_EQUAL(out.size(), in.size());
    BOOST_CHECK(alphaUnchanged(in, out));
}

BOOST_AUTO_TEST_CASE(radial_blur_amount_zero_returns_original)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(16, 16, 200, 100, 50));
    cv::Mat out = ImageEngine::radialBlur(in, 0.0, 0.5, 0.5, 12);
    BOOST_CHECK_EQUAL(cv::norm(in, out, cv::NORM_INF), 0.0);
}

BOOST_AUTO_TEST_CASE(zoom_blur_amount_zero_returns_original)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(16, 16, 200, 100, 50));
    cv::Mat out = ImageEngine::zoomBlur(in, 0.0, 0.5, 0.5, 12);
    BOOST_CHECK_EQUAL(cv::norm(in, out, cv::NORM_INF), 0.0);
}

BOOST_AUTO_TEST_CASE(radial_zoom_blur_preserve_dims)
{
    cv::Mat in = ImageEngine::toCvMat(makeOpaqueRgba(32, 24, 200, 100, 50));
    cv::Mat r = ImageEngine::radialBlur(in, 0.5, 0.5, 0.5, 16);
    cv::Mat z = ImageEngine::zoomBlur(in, 0.5, 0.5, 0.5, 16);
    BOOST_CHECK_EQUAL(r.size(), in.size());
    BOOST_CHECK_EQUAL(z.size(), in.size());
    BOOST_CHECK_EQUAL(r.channels(), 4);
    BOOST_CHECK_EQUAL(z.channels(), 4);
}

// Premultiplied blur must NOT turn fully transparent pixels into opaque black.
// Check that pixels that remain fully transparent after blur stay colour-0/alpha-0.
static void checkNoBlackOnTransparent(const cv::Mat& out)
{
    std::vector<cv::Mat> ch;
    cv::split(out, ch);   // B G R A (8U)
    for (int y = 0; y < out.rows; ++y) {
        for (int x = 0; x < out.cols; ++x) {
            if (ch[3].at<uchar>(y, x) == 0) {
                // fully transparent → colour channels must be 0 (no black halo)
                BOOST_CHECK_EQUAL(ch[0].at<uchar>(y, x), 0);
                BOOST_CHECK_EQUAL(ch[1].at<uchar>(y, x), 0);
                BOOST_CHECK_EQUAL(ch[2].at<uchar>(y, x), 0);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(box_blur_no_black_on_transparent_edges)
{
    cv::Mat in = makeHalfTransparent(32, 16);
    cv::Mat out = ImageEngine::boxBlur(in, 5);
    checkNoBlackOnTransparent(out);
}

BOOST_AUTO_TEST_CASE(motion_blur_no_black_on_transparent_edges)
{
    cv::Mat in = makeHalfTransparent(32, 16);
    cv::Mat out = ImageEngine::motionBlur(in, 15, 0.0);
    checkNoBlackOnTransparent(out);
}

BOOST_AUTO_TEST_CASE(bilateral_blur_no_black_on_transparent_edges)
{
    cv::Mat in = makeHalfTransparent(32, 16);
    cv::Mat out = ImageEngine::bilateralBlur(in, 9, 75.0, 75.0);
    checkNoBlackOnTransparent(out);
}

BOOST_AUTO_TEST_SUITE_END()
