#include "logger.h"

#include <QTextEdit>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTimer>
#include <QPointer>
#include <QTextDocument>
#include <deque>

#include "spdlog/sinks/rotating_file_sink.h"

// ============================================================
// Global state (file-scope, not exported)
// ============================================================
static std::shared_ptr<spdlog::logger> s_logger;
static std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> s_fileSink;
static QString s_logFilePath;
static QPointer<QTextEdit> s_uiTextEdit;
static spdlog::level::level_enum s_uiLevel = spdlog::level::debug;
static size_t s_lastRenderedIndex = 0;   // for incremental append
static spdlog::level::level_enum s_lastRenderedLevel = spdlog::level::debug;  // to detect level change

// ============================================================
// Qt 全局消息处理：将 qDebug/qInfo/qWarning/qCritical 路由到 spdlog
// ============================================================
static void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    auto logger = spdlog::default_logger();
    if (!logger) return;

    switch (type) {
    case QtDebugMsg:    logger->debug(msg.toStdString()); break;
    case QtInfoMsg:     logger->info(msg.toStdString());  break;
    case QtWarningMsg:  logger->warn(msg.toStdString());  break;
    case QtCriticalMsg: logger->error(msg.toStdString()); break;
    case QtFatalMsg:    logger->critical(msg.toStdString()); break;
    }
}

// ============================================================
// Session log buffer sink: stores formatted messages for re-rendering
// ============================================================
#include <vector>

struct LogEntry {
    QString formatted;
    spdlog::level::level_enum level;
};

// Writers and UI snapshots share this lock; rendering never holds it.
static constexpr size_t kSessionCapacity = 10000;
static std::mutex s_sessionMutex;
static std::deque<LogEntry> s_sessionBuffer;
static size_t s_firstSequence = 0;

class SessionBufferSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        QString text = QString::fromUtf8(buf.data(), static_cast<int>(buf.size()));
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        s_sessionBuffer.push_back({text, msg.level});
        if (s_sessionBuffer.size() > kSessionCapacity) {
            s_sessionBuffer.pop_front();
            ++s_firstSequence;
        }
    }
    void flush_() override {}
};

// ============================================================
// Re-render session logs filtered by level
// ============================================================
void rerenderSessionLogs()
{
    if (!s_uiTextEdit) return;

    spdlog::level::level_enum minLevel = uiLogLevel();
    bool levelChanged = (minLevel != s_lastRenderedLevel);

    std::vector<LogEntry> pending;
    size_t nextSequence;
    bool reset;
    {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        reset = levelChanged || s_lastRenderedIndex < s_firstSequence;
        const size_t start = reset ? s_firstSequence : s_lastRenderedIndex;
        nextSequence = s_firstSequence + s_sessionBuffer.size();
        if (start < nextSequence) {
            pending.assign(s_sessionBuffer.begin() + (start - s_firstSequence),
                           s_sessionBuffer.end());
        }
    }
    if (reset) s_uiTextEdit->clear();
    s_lastRenderedLevel = minLevel;
    if (pending.empty()) {
        s_lastRenderedIndex = nextSequence;
        return;
    }

    QTextCursor cursor(s_uiTextEdit->textCursor());
    cursor.movePosition(QTextCursor::End);

    auto levelColor = [](spdlog::level::level_enum lv) -> QColor {
        switch (lv) {
        case spdlog::level::trace:    return QColor(160, 160, 160);
        case spdlog::level::debug:    return QColor(160, 160, 160);  // gray
        case spdlog::level::info:     return Qt::black;               // black
        case spdlog::level::warn:     return QColor(255, 165, 0);    // orange
        case spdlog::level::err:      return Qt::red;                 // red
        case spdlog::level::critical: return Qt::red;
        default: return Qt::black;
        }
    };

    // Incremental: only render new entries
    for (const auto& entry : pending) {
        if (entry.level < minLevel) continue;

        QTextCharFormat fmt;
        fmt.setForeground(levelColor(entry.level));
        cursor.setCharFormat(fmt);
        cursor.insertText(entry.formatted + "\n");
    }
    s_lastRenderedIndex = nextSequence;
}

// ============================================================
// Public API
// ============================================================

void setupLogging(QTextEdit* uiTextEdit, const QString& logDir)
{
    s_uiTextEdit = uiTextEdit;
    {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        s_sessionBuffer.clear();
        s_firstSequence = 0;
    }
    s_lastRenderedIndex = 0;
    s_lastRenderedLevel = s_uiLevel;
    if (uiTextEdit) {
        uiTextEdit->clear();
        uiTextEdit->document()->setMaximumBlockCount(static_cast<int>(kSessionCapacity));
    }

    // 1) Create rotating file sink: 5MB max, 3 rotated files
    QDir dir(logDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    s_logFilePath = dir.filePath("app.log");
    s_fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        s_logFilePath.toStdString(), 5 * 1024 * 1024, 3);

    // 2) Create session buffer sink for colored rendering
    auto sessionSink = std::make_shared<SessionBufferSink>();
    sessionSink->set_pattern("%Y-%m-%d %H:%M:%S [%l] %v");

    // 3) Create multi-sink logger (session buffer + file only, no qt_color_sink)
    std::vector<spdlog::sink_ptr> sinks = {sessionSink, s_fileSink};
    s_logger = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());
    s_logger->set_pattern("%Y-%m-%d %H:%M:%S [%l] %v");
    s_logger->set_level(spdlog::level::debug);

    spdlog::set_default_logger(s_logger);
    spdlog::set_level(spdlog::level::debug);

    // 4) Install Qt message handler to redirect qDebug etc.
    qInstallMessageHandler(qtMessageHandler);

    // 5) Timer to re-render logs with colors every 100ms
    auto* timer = new QTimer(uiTextEdit);
    QObject::connect(timer, &QTimer::timeout, uiTextEdit, []() {
        rerenderSessionLogs();
    });
    timer->start(100);
}

void setUILogLevel(spdlog::level::level_enum level)
{
    s_uiLevel = level;
}

spdlog::level::level_enum uiLogLevel()
{
    return s_uiLevel;
}

QString logFilePath()
{
    return s_logFilePath;
}

void flushLogs()
{
    if (s_logger) {
        s_logger->flush();
    }
}

bool openLogFolder(bool selectFile)
{
    if (s_logFilePath.isEmpty()) return false;
    QFileInfo fileInfo(s_logFilePath);

    if (selectFile && fileInfo.exists())
    {
#ifdef Q_OS_WIN
        QProcess::startDetached("explorer",
                                QStringList() << "/select," << QDir::toNativeSeparators(fileInfo.absoluteFilePath()));
#elif defined(Q_OS_MAC)
        QProcess::startDetached("open",
                                QStringList() << "-R" << fileInfo.absoluteFilePath());
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
#endif
    }
    else
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
    }

    return true;
}

void clearLogUi()
{
    if (!s_uiTextEdit) return;
    s_uiTextEdit->clear();
    std::lock_guard<std::mutex> lock(s_sessionMutex);
    s_lastRenderedIndex = s_firstSequence + s_sessionBuffer.size();
}
