# EdgeVision 重构记录（2026-10-31）

## 第二轮：窗口职责、目录与会话细节

开始修改前按要求提交上一轮：`fc45257`（`refactor: isolate pipeline execution and dashboard runtime with UI regressions`）。本节对应之后的独立工作区改动。

- MainWindow 缩减为 357 行，保留窗口展示、组件构造和顶层接线。图片会话/预览、检测页面装配、远程命令/采集分别进入 `InspectionSession`、`DetectionWorkspace`、`RemoteInspectionController`，不向新组件传入 MainWindow 或 Designer UI。
- `include/application` 与 `src/application` 镜像组织；图片列表控制器移到 `controllers`；第三方依赖与 UI 测试规则分别提取到 `cmake/ThirdParty.cmake` 和 `cmake/UiTests.cmake`。源文件仍显式登记。
- 删除图片只发布一次切换；剩余图片恢复自己的检测页面。删除最后一张及全量清空同步清除列表、画布、旧结果和待处理预览；图片 ID 计数不回退。
- 切图保存旧配置，替换图片和重开步骤页保留当前编辑。图片切换清理旧绘制覆盖层，回填不执行。移除无效的 Config/Execute 状态和立即清除的脏标记。
- 批量任务在开始时保存图像与 ROI 快照，避免重新读取旧磁盘文件或受到后续图片删除/替换影响。
- 远程采集限制一个任务在途，独立相机在后台采集，使用默认设备 0；完成后核验图片、显示代次与配置，过期帧丢弃。已打开视频使用缓存帧，不抢读下一帧。
- Scheduler 和自动检测具有显式关闭/等待流程；销毁窗口控件前停止视频和后台任务。统一页面切换动画，并用状态栏自身超时替代鼠标移动时累积的清除定时器。
- ROI 边界先求交集再取整，拒绝 NaN、非正尺寸和越界区域；批量检测对无效 ROI 明确失败，交互预览也不回退到整图。修复零尺寸 QLabel 坐标映射的除零入口。

验证：

- 主程序完整构建成功：`build/msvc-buildtools-debug-v2/bin/EdgeVision.exe`。
- `test-msvc.cmd`：6/6 通过（6.86 秒）。
- `test-ui-msvc.cmd`：2/2 通过（6.02 秒），原有 14 个交互场景与新增 9 个应用流程场景通过；含真实 MainWindow 正常析构。QtTest 连同初始化/清理分别为 16/16、11/11。
- 上一轮提交前复验云端：97 项通过；本轮没有修改云端代码。
- `git diff --check` 通过；新增应用源文件全部在 CMake 注册。

边界：后台采集测试使用注入函数，未验收真实相机/Broker、CUDA 或视觉布局。图像快照会增加批量运行的内存占用；关闭需等待驱动/算法返回。现存 ROI 持久化实际采用像素坐标，与归一化规范存在历史差异，本轮不改变旧配置格式；版本化迁移另行设计。模块边界详见 [architecture.md](architecture.md)。

## 第一轮

本轮覆盖桌面执行层、界面交互、云端结构和构建验证。按后续要求，优先清理 Slot by name 与界面事件。开始前已有的 letterbox、配置解析和测试改动保留；没有提交或发布。

## 界面交互与信号槽

- 禁用 AUTOUIC 自动接线，主窗口、图像、增强、视频、日志页采用函数指针显式连接；移除 `on_控件_信号` 命名和无效空槽。
- `ISignalConnectable` 对每个实例只接线一次；重复初始化不会累积执行回调。
- 配置回填与编辑分开：增强、过滤、提取、直线、条码等页面回填不修改 Pipeline、不触发执行。颜色过滤完整保留 Gray/RGB/HSV 参数。
- 增强“重置”只影响增强配置；撤销支持未保存的修改，并在切换配置时建立新的历史基线。通道、滤波应用、直线滑块与 SpinBox、参考线绘制/清除去掉重复执行。
- 修正条码/直线页面查找名称；按 ROI ID + 检测项 ID 写回，多个同类检测项互不覆盖。OCR 判定字段也在选择检测项时回填。
- 图片列表回填阻断选择信号，并同步 current item，避免切图后跳回旧图。ROI 树支持鼠标/键盘统一选择，刷新保留检测项；新增检测项后选中新项。
- 步骤页回填不再改变其他检测页的可见性；自定义检测项选择保留步骤。配置导出以当前快照为基础，保留尚未创建页面的字段。
- 批量暂停/恢复限制单任务在途；暂停时停止可以结束，未处理图片不计为失败。结果展示使用传入上下文中的区域数。

约定详见 [UI 交互边界](ui-interactions.md)。

## 桌面执行层

- `PipelineExecutor` 按任务快照执行，线程各自持有算法/OCR实例；算法队列只有配置中的一个数据源。交互、自动与批量调用同一执行器。
- `ObjectDetectionService` 独立持有每线程 ORT/DNN 模型；Widget 负责参数和显示，不再做后台推理。评估器通过推理函数接收每项参数，模型失败不能按零目标通过。
- 请求拥有图像和不可变配置，并携带图片、ROI、显示代次、64 位 ID 与时间戳。结果通过归属检查后才能发布；显示画框不修改原始结果图像。
- 调度器明确区分可靠任务与最新预览；队列拒绝和取消有通知，连续帧不会重置等待窗口造成饥饿。
- 日志采用同一把锁读取/写入缓冲，锁外渲染、容量有界，日志控件销毁后不会被访问。
- 修复中文 MSVC include 前缀导致 Ninja 漏追踪头文件依赖。桌面程序与交互测试链接相同的 `EdgeVisionRuntime`，依赖仍来自仓库。

## 云端

- `create_app` 工厂、`DashboardRuntime` 生命周期、独立 state、公共/管理 Blueprint 与 `Database` 实例分层。导入模块不打开数据库、不启动网络或线程。
- MQTT 异步连接、重连、停止与设备超时线程统一归运行时所有；支持测试注入客户端，不依赖真实 Broker。
- 管理 SQL 位于存储层，写入事务成功后更新缓存；缓存重载先完成读取，再统一发布。数据库文件可用 `DASHBOARD_DB_PATH` 配置，Docker 使用持久卷路径。
- 保持既有 API 路由和 MQTT topic；直接执行 `python app.py` 管理启停，嵌入其他 WSGI 服务器需显式管理 runtime。

## 验证

- `test-msvc.cmd`：6/6 CTest 目标通过（最新 13.67 秒），含几何、配置、调度、并发日志、生产条码页和真实 pin ONNX 推理。
- `test-ui-msvc.cmd`：1/1 CTest 目标通过（2.25 秒），14 个生产 QWidget 交互场景全部通过；QtTest 连同初始化与清理为 16/16。报告位于 `build/msvc-buildtools-debug-v2/ui-interaction-results.txt`。
- 云端：97 项 pytest 通过；独立数据库、多实例隔离、生命周期重试、事务回滚和缓存读取失败均有回归。
- 主程序完整构建通过，输出 `build/msvc-buildtools-debug-v2/bin/EdgeVision.exe`。

测试使用临时 SQLite 和模拟 MQTT 客户端；未修改生产数据库。Qt offscreen 验证交互与生命周期，不等于真实相机、连续视频、CUDA 或模型准确率验收。此前启动检查为 offscreen 运行后主动终止；没有将它计作正常关闭验收。
