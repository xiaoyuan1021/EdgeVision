#ifndef DISPLAY_MODE_MANAGER_H
#define DISPLAY_MODE_MANAGER_H

#include <QObject>
#include <QString>

#include "config/display_config.h"

class QTabWidget;
class PipelineManager;
class ImageView;

/**
 * 显示模式管理器 - 负责Tab页面与Pipeline显示模式之间的映射与切换
 *
 * 职责：
 * 1. 从 TabRegistry 查询 Tab名称 → DisplayMode 映射
 * 2. 根据当前Tab切换Pipeline的显示模式
 * 3. 管理快速渲染（利用上次Pipeline结果，不重新执行）
 *
 */
class DisplayModeManager : public QObject
{
    Q_OBJECT

public:
    explicit DisplayModeManager(QTabWidget* tabWidget,
                                PipelineManager* pipelineManager,
                                ImageView* view,
                                QObject* parent = nullptr);

    /// 获取指定Tab的显示模式
    DisplayConfig::Mode getModeForTab(int index) const;

public slots:
    /// 显示模式改变时重绘已有结果；没有结果时保持原图，不执行 Pipeline。
    void onTabChanged(int index);

    /// 在Pipeline执行前调用：根据当前Tab设置Pipeline的显示模式
    void applyModeForCurrentTab();

private:
    /// 用上次Pipeline结果快速渲染当前显示模式
    void displayCurrentResult();

    QTabWidget* m_tabWidget;
    PipelineManager* m_pipelineManager;
    ImageView* m_view;

    /// 当前显示模式，用于判断Tab切换是否需要刷新
    DisplayConfig::Mode m_lastMode = DisplayConfig::Mode::Original;
};

#endif // DISPLAY_MODE_MANAGER_H
