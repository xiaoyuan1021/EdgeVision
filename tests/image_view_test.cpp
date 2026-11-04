#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QDir>
#include <QStatusBar>
#include <QMainWindow>
#include "ui/image_view.h"
#include "ui/image_view_controls.h"
#include "core/pipeline_manager.h"
#include "core/roi_manager.h"
#include "widgets/extract_tab_widget.h"

using Mode = ImageView::DrawingMode;

static QImage sample()
{
    QImage image(240, 180, QImage::Format_RGB32);
    image.fill(QColor("#DCE7ED"));
    return image;
}

static void showImage(ImageView& view)
{
    view.resize(640, 480);
    view.show();
    view.setImage(sample());
    view.zoomActualSize();
    QCoreApplication::processEvents();
}

static void click(ImageView& view, QPointF imagePoint, Qt::MouseButton button = Qt::LeftButton)
{
    QTest::mouseClick(view.viewport(), button, {}, view.mapFromScene(imagePoint));
}

static void drag(ImageView& view, QPointF from, QPointF to)
{
    QTest::mousePress(view.viewport(), Qt::LeftButton, {}, view.mapFromScene(from));
    // Release must capture its endpoint even when no intermediate move was delivered.
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, view.mapFromScene(to));
}

class ImageViewTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QFile style(":/style.qss");
        QVERIFY(style.open(QFile::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }

    void toolsRequireImageAndVideoLockCancelsDraft() {
        ImageView view;
        view.startPolygonDrawing("template");
        QCOMPARE(view.drawingMode(), Mode::None);
        showImage(view);
        view.setRoiMode(true);
        drag(view, {20, 20}, {100, 100});
        QSignalSpy roi(&view, &ImageView::roiSelected);
        view.setZoomEnabled(false);
        QCOMPARE(view.drawingMode(), Mode::None);
        view.confirmDrawing();
        view.startReferenceLineDrawing();
        QVERIFY(roi.isEmpty());
        QCOMPARE(view.drawingMode(), Mode::None);
        view.setZoomEnabled(true);
        view.startReferenceLineDrawing();
        QCOMPARE(view.drawingMode(), Mode::ReferenceLine);
    }

    void referenceLineCanStartAtOriginAndCancelKeepsConfirmedLine() {
        ImageView view;
        showImage(view);
        int count = 0;
        cv::Point2f start, end;
        connect(&view, &ImageView::referenceLineDrawn, &view,
            [&](const cv::Point2f& a, const cv::Point2f& b) { ++count; start = a; end = b; });
        view.startReferenceLineDrawing();
        click(view, {0, 0});
        click(view, {0, 0}); // Zero length must not be accepted.
        view.confirmDrawing();
        QCOMPARE(count, 0);
        click(view, {160, 100});
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(count, 1);
        QCOMPARE(start, cv::Point2f(0, 0));
        QCOMPARE(end, cv::Point2f(160, 100));
        QCOMPARE(view.drawingMode(), Mode::None);
        view.startReferenceLineDrawing();
        click(view, {50, 50});
        click(view, {100, 100});
        QTest::keyClick(&view, Qt::Key_Escape);
        int lines = 0;
        for (auto* item : view.scene()->items())
            if (auto* line = dynamic_cast<QGraphicsLineItem*>(item)) {
                ++lines;
                QCOMPARE(line->line(), QLineF(0, 0, 160, 100));
            }
        QCOMPARE(lines, 1);
        QCOMPARE(count, 1);
    }

    void toolsAreExclusiveAndCompletionCanStartTheNextTool() {
        ImageView view;
        showImage(view);
        QSignalSpy polygon(&view, &ImageView::polygonDrawingFinished);
        QSignalSpy roi(&view, &ImageView::roiSelected);
        view.startPolygonDrawing("template");
        click(view, {10, 10});
        view.setRoiMode(true);
        QVERIFY(view.getPolygonPoints().isEmpty());
        drag(view, {20, 30}, {120, 110});
        connect(&view, &ImageView::roiSelected, &view, [&](const QRectF&) {
            view.startRectangleDrawing("region");
        });
        view.confirmDrawing();
        QCOMPARE(roi.size(), 1);
        QCOMPARE(view.drawingMode(), Mode::Rectangle);
        QVERIFY(polygon.isEmpty());
        drag(view, {30, 40}, {140, 130});
        QCOMPARE(polygon.size(), 1);
        QCOMPARE(polygon.first().at(0).toString(), QString("region"));
        QCOMPARE(polygon.first().at(1).value<QVector<QPointF>>(),
            QVector<QPointF>({{30, 40}, {140, 40}, {140, 130}, {30, 130}}));
        QCOMPARE(view.drawingMode(), Mode::None);
    }

    void roiMoveAndResizeStayWithinImage_data() {
        QTest::addColumn<QPointF>("from");
        QTest::addColumn<QPointF>("to");
        QTest::addColumn<QRectF>("expected");
        QTest::newRow("move outside") << QPointF(90, 80) << QPointF(-90, -80) << QRectF(0, 0, 100, 80);
        QTest::newRow("bottom right outside") << QPointF(140, 120) << QPointF(500, 500) << QRectF(40, 40, 200, 140);
        QTest::newRow("left crosses right") << QPointF(40, 80) << QPointF(230, 80) << QRectF(137, 40, 3, 80);
        QTest::newRow("top crosses bottom") << QPointF(90, 40) << QPointF(90, 170) << QRectF(40, 117, 100, 3);
    }

    void roiMoveAndResizeStayWithinImage() {
        QFETCH(QPointF, from);
        QFETCH(QPointF, to);
        QFETCH(QRectF, expected);
        ImageView view;
        showImage(view);
        QSignalSpy selected(&view, &ImageView::roiSelected);
        view.setRoiMode(true);
        drag(view, {40, 40}, {140, 120});
        drag(view, from, to);
        view.confirmDrawing();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first().first().toRectF(), expected);
        QVERIFY(QRectF(0, 0, 240, 180).contains(expected));
    }

    void emptyClicksAndTinyRoiRemainRetryable() {
        ImageView view;
        showImage(view);
        QSignalSpy selected(&view, &ImageView::roiSelected);
        view.setRoiMode(true);
        drag(view, {-20, 10}, {100, 100});
        view.confirmDrawing();
        QVERIFY(selected.isEmpty());
        drag(view, {20, 20}, {21, 21});
        QCOMPARE(view.drawingMode(), Mode::Roi);
        drag(view, {20, 20}, {80, 60});
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first().first().toRectF(), QRectF(20, 20, 60, 40));
    }

    void polygonUndoRejectsDegenerateShapeAndKeepsConfirmedSelection() {
        ImageView view;
        showImage(view);
        QSignalSpy selected(&view, &ImageView::polygonDrawingFinished);
        view.startPolygonDrawing("template");
        click(view, {10, 10});
        click(view, {50, 10});
        click(view, {90, 10});
        view.confirmDrawing();
        QVERIFY(selected.isEmpty());
        QTest::keyClick(&view, Qt::Key_Backspace);
        QCOMPARE(view.getPolygonPoints().size(), 2);
        click(view, {80, 100});
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(view.drawingMode(), Mode::None);
        QCOMPARE(view.getPolygonPoints().size(), 3);
        view.setImageKeepZoom(sample());
        QCOMPARE(view.getPolygonPoints().size(), 3);
        view.setImage(sample()); // Explicit identity/ROI switch, even at the same size.
        QVERIFY(view.getPolygonPoints().isEmpty());
    }

    void sameSizePreviewKeepsDraftButReplacementCancelsIt() {
        ImageView view;
        showImage(view);
        QSignalSpy selected(&view, &ImageView::roiSelected);
        view.setRoiMode(true);
        drag(view, {20, 20}, {90, 90});
        view.setImageKeepZoom(sample());
        QCOMPARE(view.drawingMode(), Mode::Roi);
        view.confirmDrawing();
        QCOMPARE(selected.size(), 1);
        view.startPolygonDrawing("template");
        click(view, {10, 10});
        view.setImageKeepZoom(sample().scaled(120, 90));
        QCOMPARE(view.drawingMode(), Mode::None);
        QVERIFY(view.getPolygonPoints().isEmpty());
        view.startReferenceLineDrawing();
        view.clear();
        QCOMPARE(view.drawingMode(), Mode::None);
        QCOMPARE(view.scene()->items().size(), 1);
    }

    void pixelReadoutDoesNotClampEmptyCanvasToImageEdge() {
        ImageView view;
        showImage(view);
        QSignalSpy pixels(&view, &ImageView::pixelInfoChanged);
        const auto sendMove = [&](QPointF point) {
            const QPoint local = view.mapFromScene(point);
            QMouseEvent event(QEvent::MouseMove, QPointF(local),
                QPointF(view.viewport()->mapToGlobal(local)), Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(view.viewport(), &event);
        };
        sendMove({-10, 20});
        QVERIFY(pixels.isEmpty());
        sendMove({0, 0});
        QCOMPARE(pixels.size(), 1);
        QCOMPARE(pixels.first().at(0).toInt(), 0);
        QCOMPARE(pixels.first().at(1).toInt(), 0);
    }

    void extractClearButtonCancelsTheRectangleTool() {
        PipelineManager pipeline;
        RoiManager rois;
        rois.setFullImage(cv::Mat(180, 240, CV_8UC3, cv::Scalar(40)));
        ImageView view;
        showImage(view);
        QStatusBar status;
        ExtractTabWidget page(&pipeline, &view, &rois, &status);
        page.initialize();
        page.findChild<QPushButton*>("btn_drawRegion")->click();
        QCOMPARE(view.drawingMode(), Mode::Rectangle);
        page.findChild<QPushButton*>("btn_clearRegion")->click();
        QCOMPARE(view.drawingMode(), Mode::None);
        QCOMPARE(pipeline.scheduler()->pendingCount(), 0);
    }

    void drawingHintAndCancelAreVisibleWithoutScalingWithImage() {
        QMainWindow shell;
        auto* central = new QWidget(&shell);
        shell.setCentralWidget(central);
        auto* layout = new QVBoxLayout(central);
        auto* view = new ImageView(central);
        auto* controls = new ImageViewControls(view, central);
        layout->addWidget(controls);
        layout->addWidget(view, 1);
        shell.resize(800, 600);
        shell.show();
        view->setImage(sample());
        view->setRoiMode(true);
        drag(*view, {20, 20}, {150, 120});
        auto* hint = controls->findChild<QLabel*>("label_drawingHint");
        auto* cancel = controls->findChild<QToolButton*>("btn_cancelDrawing");
        QVERIFY(hint->isVisible());
        QVERIFY(hint->text().contains("Enter"));
        const auto font = hint->font();
        view->zoomIn();
        QCOMPARE(hint->font(), font);
        const auto output = qEnvironmentVariable("EDGEVISION_UI_CAPTURE_DIR");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QTest::qWait(50);
            QVERIFY(shell.grab().save(output + "/drawing-roi.png"));
        }
        cancel->click();
        QCOMPARE(view->drawingMode(), Mode::None);
        QVERIFY(!hint->isVisible());
    }
};

QTEST_MAIN(ImageViewTest)
#include "image_view_test.moc"
