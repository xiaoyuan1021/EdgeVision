# 桌面模块职责与生命周期

`include/` 与 `src/` 按同一模块目录镜像组织。新增源文件仍在根 `CMakeLists.txt` 的 `HEADERS` / `SOURCES` 显式登记；第三方链接和 DLL 部署在 `cmake/ThirdParty.cmake`，生产 QWidget 测试目标在 `cmake/UiTests.cmake`。

| 目录 | 职责 | 边界 |
| --- | --- | --- |
| `ui` | 主窗口、画布、日志、通知、状态栏等窗口组件 | MainWindow 负责构造、连接和窗口展示，不实现采集、检测或图片会话规则 |
| `application` | 跨控制器的应用流程 | 不包含 `ui_mainwindow.h`，不接收 MainWindow 指针 |
| `controllers` | 图片列表、ROI、配置、方案、自动检测、结果呈现 | 图片列表控制器不再放在 Widget 目录；通过领域事件同步状态 |
| `widgets` | 功能页面、对话框和页面工厂 | 通过配置/结果/接线接口参与流程 |
| `core` | 图像与 ROI 状态、执行器、调度、视频、MQTT | 工作线程使用拥有的数据快照 |
| `algorithm` | 图像处理、推理与几何变换 | 不依赖主窗口 |
| `config` / `data` | 配置、序列化及结果数据 | 不保存 UI 对象 |

## 应用编排

- `InspectionSession`：保存切出图片的配置，失效旧结果，恢复切入图片配置与画布；统一提交预览请求。切图和配置回填本身不执行检测。
- `DetectionWorkspace`：懒加载页面的接线/配置注册/初始化、检测项选择、步骤页与检测页可见性。结果处理器先构造，再允许创建视频页面。
- `RemoteInspectionController`：转发 MQTT 命令与报告，管理单个在途采集。已打开的视频使用缓存帧；独立相机在工作线程打开和采集，完成时检查图片身份、显示代次与配置快照。

MainWindow 不再持有无实际作用的 Config/Execute 状态和“设置后立即清除”的脏标记。预览合并由 Scheduler 管理。构造函数不主动显示窗口，显示由入口负责；MQTT 可注入配置以支持离线启动和测试。

## 检测项配置与执行

`DetectionItem` 保存类型专用字段和可选的完整 `pipelineSnapshot`。`resolvedPipeline` 是界面与批量运行共用的解析入口；旧文件缺失快照时沿用 ROI 配置和对应类型的默认步骤。专用编辑器拥有的字段覆盖快照中相应字段。选择其他项、导出配置和保存方案前，由 RoiUiController 保存当前编辑。

`RoiInspectionRunner` 遍历启用的检测项，每项单独运行预处理并调用 DetectionEvaluator 判定，失败归属到具体项。没有检测项的旧 ROI 仍执行它自己的 Pipeline。全量图片配置额外保存每张图片的 Pipeline；方案模板继续按归一化矩形适配分辨率。

## 检测结果与报告

`InspectionRecord` 保留自动检测的图片级结果及执行状态、完成时间、耗时和轮次；每项结果仍携带 ROI 和检测项身份。MQTT 上报保持既有接口，本地记录另外保存完整层级。

完整检测开启新批次；失败复检按图片 ID 取最后一次已完成判定，忽略中断留下的未执行记录，追加新轮次而不覆盖历史。每次启动都先请求保存当前编辑，再克隆当前图片和配置。任务、统计和 MQTT 报告索引只属于当前轮次；完整记录通过本轮起始偏移写入。无可执行 ROI 或检测项的图片不生成默认合格结果。

`InspectionReportExporter` 只处理数据与文件，不依赖控件或控制器；CSV 展开检测项，JSON 保存完整结构。`InspectionResultsDialog` 接收记录快照，负责搜索、筛选和导出交互。`InspectionUiBinding` 负责将实际检测入口的信号连接到按钮、状态栏和结果窗口，图片定位仍由 RoiManager 完成。

结果树以轮次和图片身份区分历史节点，复检按钮由接线层根据运行状态和最新失败判定启用。导出包含全部轮次，不受界面筛选影响；停止、删除工作区图片和复检通过均不改写旧记录。

## 画布展示

`ImageView` 的图像、缩放、适配与空态实现在 `image_view_viewport.cpp`；绘制状态/几何在 `image_view_drawing.cpp`，鼠标与键盘分派在 `image_view.cpp`。缩放以 QGraphicsView 的变换为准，绘制用互斥 `DrawingMode` 和各工具必要的阶段数据，不使用可同时为真的多个工具开关。`ImageViewControls` 是独立展示组件，直接连接视图的操作和状态信号，MainWindow 只装配一次。

会话切图与 ROI 切换重新适配坐标空间；Pipeline 结果、缓存显示、增强预览和视频帧更新使用保持视野的入口，同尺寸不会覆盖用户缩放。左右面板的滚动容器由 Designer UI 定义，TabManager 仍持有原页面实例，不引入额外的页面包装映射。

## 状态转换

图片切换和删除由 `RoiManager` 唯一决定下一张图片；重复选择当前图片没有副作用。`imageSwitching` 在旧图片仍存在时发出，`currentImageChanged` 在新状态发布后发出，最后一张删除时传递空 ID。`clear()` 同时通知图片列表和会话，图片计数器不回退，避免短时间重用 ID。导入配置直接安装保存状态并刷新当前会话，不发出用于保存旧编辑的 `imageSwitching`；即使图片 ID 相同也重新回填。

批量检测统一由 `AutoDetectionController` 调度，在启动时克隆图像并保存 ROI 配置。之后删除图片、替换内存帧或修改磁盘文件不会改变已启动批次。代价是批量运行期间保留额外图像内存。结果窗口只读取记录，不另建任务队列、加载图片或执行算法；无生产入口的旧批量面板已移除。

ROI 裁剪先求左右/上下边界与图像的交集，再做整数转换；无效浮点、非正尺寸、完全越界返回空矩形。批量检测把空 ROI 判为失败，不回退到整图。当前持久化 `RoiConfig::roiRect` 与画布仍沿用图像像素坐标；规范中的全量归一化目标需要版本化配置迁移，不能将既有像素配置直接当作 0–1 坐标读取。

## 关闭顺序

1. 停止接收远程采集，等待独立采集任务完成。
2. 停止视频生产帧。
3. 停止自动检测并等待当前工作任务结束。
4. 停止 Scheduler，取消待处理请求，断开完成回调，等待在途工作结束，拒绝后续提交。
5. 在服务和 ROI 状态仍存活时销毁主窗口的 central widget，再由 QObject 清理子服务。

算法和相机驱动没有强制中断机制，关闭会等待正在执行的调用返回。测试不把取消队列描述为取消算法。

## 验证

运行 `test-msvc.cmd` 验证核心目标，运行 `test-ui-msvc.cmd` 验证 `ui_interaction`、`application_workflow`、`image_view` 和 `inspection_results`。应用流程覆盖真实 MainWindow 正常析构、图片会话、配置保留、页面恢复、批量快照、ROI 边界、异步采集与调度器关闭；画布套件直接操作生产视图的鼠标、键盘及工具条。相机采集使用注入函数，不连接真实 Broker 或摄像头。
