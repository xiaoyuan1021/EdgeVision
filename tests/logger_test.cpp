#include <QtTest>
#include <QTextEdit>
#include <QTemporaryDir>
#include <atomic>
#include <thread>
#include "core/logger.h"

class LoggerTest : public QObject {
    Q_OBJECT
private slots:
    void concurrentLoggingRemainsBoundedAndRerenders() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto view = std::make_unique<QTextEdit>();
        setupLogging(view.get(), dir.path());
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (int i = 0; i < 12000; ++i) spdlog::debug("entry {}", i);
            spdlog::warn("retained warning");
            done.store(true);
        });
        while (!done.load()) {
            rerenderSessionLogs();
            QTest::qWait(1);
        }
        producer.join();
        rerenderSessionLogs();
        QVERIFY(view->document()->blockCount() <= 10000);
        QVERIFY(view->toPlainText().contains("retained warning"));
        setUILogLevel(spdlog::level::warn);
        rerenderSessionLogs();
        QVERIFY(!view->toPlainText().contains("entry 11999"));
        QVERIFY(view->toPlainText().contains("retained warning"));
        setUILogLevel(spdlog::level::debug);
        rerenderSessionLogs();
        QVERIFY(view->toPlainText().contains("entry 11999"));
        QVERIFY(!view->toPlainText().contains("entry 0\n"));
        view.reset();
        rerenderSessionLogs(); // QPointer prevents access after widget destruction.
    }
};
QTEST_MAIN(LoggerTest)
#include "logger_test.moc"
