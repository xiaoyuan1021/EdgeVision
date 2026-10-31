#pragma once

#include <QSignalBlocker>
#include <QWidget>
#include <vector>

// Render a complete configuration without treating intermediate control values
// as user edits. Restores every widget's previous blocking state on scope exit.
class ScopedWidgetSignals {
public:
    explicit ScopedWidgetSignals(QWidget* root) {
        const auto widgets = root->findChildren<QWidget*>();
        m_blockers.reserve(widgets.size() + 1);
        m_blockers.emplace_back(root);
        for (QWidget* widget : widgets) m_blockers.emplace_back(widget);
    }
private:
    std::vector<QSignalBlocker> m_blockers;
};
