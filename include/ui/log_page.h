#ifndef LOG_PAGE_H
#define LOG_PAGE_H

#include <QWidget>

namespace Ui {
class LogPage;
}

class LogPage : public QWidget {
    Q_OBJECT

public:
    explicit LogPage(QWidget* parent = nullptr);
    ~LogPage();

    void appendLog(const QString& message);
    void clearLog();
    
    // Initialize log page, connect to spdlog
    void initialize();

signals:
    // 请求返回主页信号
    void requestGoHome();

private slots:
    void onClearLogClicked();
    void onOpenLogClicked();
    void onHomeClicked();
    void onLogLevelChanged(int index);

private:
    void loadLogLevelConfig();
    void saveLogLevelConfig(int level);

    Ui::LogPage* m_ui;
};

#endif // LOG_PAGE_H
