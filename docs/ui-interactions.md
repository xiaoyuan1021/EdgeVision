# 界面事件与配置边界

所有 Designer 页面禁用 `uic` 自动连接（`--no-autoconnection`）。控件保留原 objectName；槽使用 `onApplyClicked` 一类语义名称，在所属 Widget 的构造或 `setupConnections()` 中以函数指针显式连接。不要再添加 `on_控件名_信号名` 或字符串版 `SIGNAL/SLOT`。

事件分成三条路径：

1. `loadFromConfig` / `set…Config`：只回填控件及本地草稿。阻断控件信号，不修改 Pipeline、不保存检测项、不触发执行。
2. 用户编辑：更新该页面拥有的配置字段。需要实时预览的滑块只通过一条值变化路径提交；Slider/SpinBox 双向绑定后只监听 Slider 执行业务处理。
3. 应用/识别：提交配置一次，再通知执行一次。`ISignalConnectable::connectSignals` 在每个实例生命周期内只接线一次，重复初始化不会累积回调。

`DetectionUiController` 在选中检测项时先建立完整配置，再回填各页；`RoiDetectionConfigController` 按 ROI ID + 检测项 ID 写回，避免修改同类型的其他检测项。树和图片列表回填用 `QSignalBlocker`，用户鼠标和键盘选择采用相同的当前项变化事件。

增强撤销历史在加载另一份配置时重置；“重置”只影响增强字段。颜色过滤保存全部 Gray/RGB/HSV 参数。配置导出从当前 Pipeline 快照开始，未创建页面的参数也能保留。

## 回归验证

在仓库根目录运行 `test-ui-msvc.cmd`。该目标链接与桌面程序相同的 `EdgeVisionRuntime`，在 Qt offscreen 平台实例化生产 Widget、操作真实控件并核验信号次数。覆盖配置回填、应用/重置/撤销、双向控件绑定、重复接线、图片和 ROI 选择、多个同类检测项、异步结果归属、批量暂停/恢复/停止。

摄像头、真实视频流、CUDA 和模型准确率仍需在对应硬件与数据上验收；offscreen 测试不替代视觉布局检查。
