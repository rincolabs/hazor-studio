#define BOOST_TEST_MODULE SelectionMaskTest
#include <boost/test/included/unit_test.hpp>

#include "core/SelectionMask.hpp"
#include <QImage>

BOOST_AUTO_TEST_SUITE(selectionmask)

BOOST_AUTO_TEST_CASE(create_and_clear)
{
    SelectionMask sm;
    BOOST_CHECK(sm.image().isNull());
    BOOST_CHECK(!sm.active());
    BOOST_CHECK(sm.isEmpty());

    sm.create(100, 80);
    BOOST_CHECK(!sm.image().isNull());
    BOOST_CHECK_EQUAL(sm.width(), 100);
    BOOST_CHECK_EQUAL(sm.height(), 80);
    BOOST_CHECK(!sm.active());
    BOOST_CHECK(sm.isEmpty());

    sm.clear();
    BOOST_CHECK(!sm.active());
    BOOST_CHECK(sm.isEmpty());
}

BOOST_AUTO_TEST_CASE(rect_replace)
{
    SelectionMask sm;
    sm.create(200, 150);

    sm.setRect(QRectF(50, 30, 80, 60), SelectMode::Replace);
    BOOST_CHECK(sm.active());
    BOOST_CHECK(!sm.isEmpty());

    // Dentro do rect - selecionado
    BOOST_CHECK(sm.isSelected(60, 40));
    BOOST_CHECK(sm.isSelected(50, 30));
    BOOST_CHECK(sm.isSelected(129, 89));

    // Fora do rect - nao selecionado
    BOOST_CHECK(!sm.isSelected(49, 30));
    BOOST_CHECK(!sm.isSelected(131, 91));
    BOOST_CHECK(!sm.isSelected(10, 10));

    // bounds engloba o rect
    QRectF b = sm.bounds();
    BOOST_CHECK(b.x() <= 50);
    BOOST_CHECK(b.right() >= 129);
    BOOST_CHECK(b.y() <= 30);
    BOOST_CHECK(b.bottom() >= 89);
}

BOOST_AUTO_TEST_CASE(rect_add_mode)
{
    SelectionMask sm;
    sm.create(200, 150);

    sm.setRect(QRectF(50, 30, 64, 49), SelectMode::Replace);
    BOOST_CHECK(sm.isSelected(60, 40));

    // Add: union de rects
    sm.setRect(QRectF(120, 20, 70, 60), SelectMode::Add);
    BOOST_CHECK(sm.isSelected(60, 40));   // ainda selecionado
    BOOST_CHECK(sm.isSelected(130, 30));  // novo rect
    BOOST_CHECK(!sm.isSelected(200, 20)); // fora dos dois

    // O primeiro rect ainda intacto
    BOOST_CHECK(sm.isSelected(50, 30));
}

BOOST_AUTO_TEST_CASE(rect_subtract_mode)
{
    SelectionMask sm;
    sm.create(200, 150);

    sm.setRect(QRectF(50, 30, 80, 60), SelectMode::Replace);
    BOOST_CHECK(sm.isSelected(60, 40));

    // Subtract: remove interior do segundo rect
    sm.setRect(QRectF(60, 40, 50, 40), SelectMode::Subtract);
    BOOST_CHECK(sm.isSelected(50, 30));  // borda do primeiro rect
    BOOST_CHECK(!sm.isSelected(70, 50)); // removido pelo subtract
    BOOST_CHECK(sm.isSelected(120, 50)); // area direita mantida
}

BOOST_AUTO_TEST_CASE(rect_intersect_mode)
{
    SelectionMask sm;
    sm.create(200, 150);

    sm.setRect(QRectF(50, 30, 80, 60), SelectMode::Replace);
    BOOST_CHECK(sm.isSelected(60, 40));

    // Intersect: apenas pixels nos dois rects
    sm.setRect(QRectF(70, 50, 100, 80), SelectMode::Intersect);
    BOOST_CHECK(!sm.isSelected(50, 30)); // primeiro rect, fora da intersec
    BOOST_CHECK(sm.isSelected(80, 60));  // na intersecao
    BOOST_CHECK(!sm.isSelected(150, 100)); // segundo rect, fora da intersec
}

BOOST_AUTO_TEST_CASE(rect_clamped_to_bounds)
{
    SelectionMask sm;
    sm.create(100, 100);

    // Rect alem das bordas deve ser clampado
    sm.setRect(QRectF(-10, -10, 120, 120), SelectMode::Replace);
    BOOST_CHECK(sm.isSelected(0, 0));
    BOOST_CHECK(sm.isSelected(99, 99));
    BOOST_CHECK(sm.isSelected(50, 50));
}

BOOST_AUTO_TEST_CASE(ellipse_basic)
{
    SelectionMask sm;
    sm.create(100, 100);

    sm.setEllipse(QRectF(25, 25, 50, 50), SelectMode::Replace);
    BOOST_CHECK(sm.active());

    // Centro da elipse: selecionado
    BOOST_CHECK(sm.isSelected(50, 50));

    // Dentro do raio (aproximadamente)
    int dx = 15, dy = 0;
    BOOST_CHECK(sm.isSelected(50 + dx, 50 + dy));

    // Fora da elipse (canto)
    BOOST_CHECK(!sm.isSelected(25, 25));
}

BOOST_AUTO_TEST_CASE(ellipse_too_small_falls_back)
{
    SelectionMask sm;
    sm.create(100, 100);

    // Elipse com raio minima → fallback para rect
    sm.setEllipse(QRectF(50, 50, 1, 1), SelectMode::Replace);
    BOOST_CHECK(sm.active());

    // O pixel central deve estar selecionado (rect 50..51)
    BOOST_CHECK(sm.isSelected(50, 50));
}

BOOST_AUTO_TEST_CASE(polygon_basic)
{
    SelectionMask sm;
    sm.create(100, 100);

    std::vector<QPointF> tri = {
        {10, 10}, {90, 10}, {50, 90}
    };
    sm.setPolygon(tri, SelectMode::Replace);
    BOOST_CHECK(sm.active());
    BOOST_CHECK(!sm.isEmpty());

    // Centro do triangulo
    BOOST_CHECK(sm.isSelected(50, 40));

    // Fora
    BOOST_CHECK(!sm.isSelected(5, 5));
    BOOST_CHECK(!sm.isSelected(95, 50));
}

BOOST_AUTO_TEST_CASE(polygon_fewer_than_3_points)
{
    SelectionMask sm;
    sm.create(100, 100);
    sm.setRect(QRectF(0, 0, 50, 50), SelectMode::Replace);
    BOOST_CHECK(sm.isSelected(10, 10));

    // 2 pontos: nao deve alterar a mask (minimo = 3)
    sm.setPolygon({{0, 0}, {50, 50}}, SelectMode::Add);
    BOOST_CHECK(sm.isSelected(10, 10)); // mask preservada
}

BOOST_AUTO_TEST_CASE(invert)
{
    SelectionMask sm;
    sm.create(100, 100);

    sm.setRect(QRectF(10, 10, 30, 30), SelectMode::Replace);
    BOOST_CHECK(sm.isSelected(15, 15));
    BOOST_CHECK(!sm.isSelected(50, 50));

    sm.invert();
    BOOST_CHECK(!sm.isSelected(15, 15));
    BOOST_CHECK(sm.isSelected(50, 50));
}

BOOST_AUTO_TEST_CASE(bounds_empty)
{
    SelectionMask sm;
    sm.create(100, 100);
    BOOST_CHECK(sm.bounds().isNull() || sm.bounds().isEmpty());

    sm.setRect(QRectF(10, 10, 30, 30), SelectMode::Replace);
    QRectF b = sm.bounds();
    BOOST_CHECK(!b.isNull());
    BOOST_CHECK(b.width() >= 29 && b.width() <= 31);
    BOOST_CHECK(b.height() >= 29 && b.height() <= 31);
}

BOOST_AUTO_TEST_CASE(resize)
{
    SelectionMask sm;
    sm.create(100, 100);
    sm.setRect(QRectF(10, 10, 30, 30), SelectMode::Replace);
    BOOST_CHECK_EQUAL(sm.width(), 100);

    sm.resize(200, 150);
    BOOST_CHECK_EQUAL(sm.width(), 200);
    BOOST_CHECK_EQUAL(sm.height(), 150);
    // Conteudo preservado na nova mask
    BOOST_CHECK(sm.isSelected(15, 15));
    // Area expandida zerada
    BOOST_CHECK(!sm.isSelected(150, 100));
}

BOOST_AUTO_TEST_CASE(active_flag)
{
    SelectionMask sm;
    sm.create(100, 100);
    BOOST_CHECK(!sm.active());

    sm.setActive(true);
    BOOST_CHECK(sm.active());

    sm.setActive(false);
    BOOST_CHECK(!sm.active());

    // setRect ativa automaticamente
    sm.setRect(QRectF(0, 0, 50, 50), SelectMode::Replace);
    BOOST_CHECK(sm.active());

    sm.clear();
    BOOST_CHECK(!sm.active());
}

BOOST_AUTO_TEST_CASE(create_from_null)
{
    SelectionMask sm;
    // Operacoes em mask nula devem ser seguras
    sm.setRect(QRectF(0, 0, 50, 50), SelectMode::Replace);
    BOOST_CHECK(sm.isEmpty()); // nada acontece

    sm.setEllipse(QRectF(0, 0, 50, 50), SelectMode::Replace);
    BOOST_CHECK(sm.isEmpty());

    sm.setPolygon({{0,0},{10,0},{5,10}}, SelectMode::Replace);
    BOOST_CHECK(sm.isEmpty());

    sm.invert();
    BOOST_CHECK(sm.isEmpty());
}

// ── bytesPerLine / cv::Mat step alignment ──────────────────────

BOOST_AUTO_TEST_CASE(set_rect_cvmat_step_matches_bytes_per_line)
{
    // SelectionMask internally uses cv::Mat wrapping QImage data.
    // If QImage::bytesPerLine() != width(), the cv::Mat step must
    // be explicitly set to avoid row misalignment.
    // Test with a non-aligned width that triggers padding.
    // Format_Grayscale8: bytesPerLine = ((w*8+31)/32)*4
    // For width=1921: ((1921*8+31)/32)*4 = (15399/32)*4 = 481*4 = 1924

    SelectionMask sm;
    sm.create(1921, 100);  // non-aligned width

    // Fill a solid rectangle
    sm.setRect(QRectF(0, 0, 1921, 100), SelectMode::Replace);

    // Every pixel should be selected
    BOOST_CHECK(sm.isSelected(0, 0));
    BOOST_CHECK(sm.isSelected(1920, 0));
    BOOST_CHECK(sm.isSelected(0, 99));
    BOOST_CHECK(sm.isSelected(1920, 99));
    BOOST_CHECK(sm.isSelected(960, 50));

    // Translate (uses cv::warpAffine internally)
    sm.translate(10, 5);
    BOOST_CHECK(!sm.isSelected(0, 0));      // old position cleared
    BOOST_CHECK(sm.isSelected(10, 5));      // new position filled
    // After translate(10,5), pixel at (1920, 99) should still be selected
    BOOST_CHECK(sm.isSelected(1920, 99));
}

BOOST_AUTO_TEST_SUITE_END()
