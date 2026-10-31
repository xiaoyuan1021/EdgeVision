#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <iostream>
#include <vector>

struct Config {
    bool enabled;
    int type;
    int count;
    bool operator==(const Config&) const = default;
};

// A minimal experiment using real Qt widgets/signals, not the production widget.
// All objects live on the main thread; default connections invoke slots directly.
bool runExperiment(bool blockSignals)
{
    QCheckBox enabled;
    QComboBox type;
    type.addItems({"auto", "QR"});
    QSpinBox count;
    count.setRange(1, 100);
    count.setValue(1);
    std::vector<Config> writes;
    int automaticCalls = 0;
    const auto save = [&] {
        Config value{enabled.isChecked(), type.currentIndex(), count.value()};
        writes.push_back(value);
        std::cout << "  write " << writes.size() << ": enabled=" << value.enabled
                  << ", type=" << type.currentText().toStdString()
                  << ", count=" << value.count << '\n';
    };
    const auto changed = [&] { ++automaticCalls; save(); };
    QObject::connect(&enabled, &QCheckBox::toggled, &enabled, changed);
    QObject::connect(&type, &QComboBox::currentIndexChanged, &enabled, changed);
    QObject::connect(&count, &QSpinBox::valueChanged, &enabled, changed);

    const auto fill = [&] {
        enabled.setChecked(true);
        type.setCurrentIndex(1);
        count.setValue(5);
    };
    std::cout << (blockSignals ? "WITH blockers\n" : "WITHOUT blockers\n");
    if (blockSignals) {
        const QSignalBlocker b1(enabled), b2(type), b3(count);
        fill();
        std::cout << "  explicit save after all controls are filled:\n";
        save(); // Blocking signals does not block ordinary function calls.
    } else {
        fill();
    }

    const std::vector<Config> expected = blockSignals
        ? std::vector<Config>{{true, 1, 5}}
        : std::vector<Config>{{true, 0, 1}, {true, 1, 1}, {true, 1, 5}};
    if (writes != expected || automaticCalls != (blockSignals ? 0 : 3)) return false;

    // Blocked emissions are not replayed on destruction of QSignalBlocker.
    QCoreApplication::processEvents();
    if (writes != expected) return false;

    count.setValue(6); // Subsequent user-style changes must still emit signals.
    if (writes.size() != expected.size() + 1 || writes.back() != Config{true, 1, 6}) return false;
    std::cout << "  PASS: expected snapshots; subsequent changes still emit\n";
    return true;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const bool without = runExperiment(false);
    const bool with = runExperiment(true);
    return without && with ? 0 : 1;
}
