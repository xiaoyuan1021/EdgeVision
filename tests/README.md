# EdgeVision tests

From the repository root on Windows/MSVC 2022:

```bat
test-msvc.cmd
```

The script discovers Visual Studio with vswhere, configures the standalone tests
with Ninja/Debug, builds, and runs CTest. Override `EDGEVISION_QT_DIR`,
`EDGEVISION_NINJA`, or `EDGEVISION_TEST_DIR` for a different installation.
Dependencies are linked from `3rdparty/`; Debug runtime DLLs must be on PATH as
for the desktop application. ONNX Runtime DLLs are loaded from the vendored folder.

Tests cover:

- `letterbox_geometry`: coordinate restoration, padding, clipping and invalid numbers.
- `config`: configuration round trips and defaults.
- `pipeline_runtime`: snapshot ownership, IDs/timestamps, task step ordering,
  bounded queues, preview coalescing, non-starvation and failed execution.
- `logger`: concurrent writes while rendering, retention bounds, level changes
  and widget destruction (offscreen Qt).
- `refactor_contract`: the production barcode widget's render/edit/apply boundary,
  preservation of hidden configuration fields, per-item detector parameters,
  and explicit inference failures (offscreen Qt).
- `object_detection_service`: actual bundled pin ONNX model without a QWidget,
  independent worker threads, invalid input and failed model replacement.
  This integration test needs the bundled model and inference runtime; it does
  not measure model accuracy or certify CUDA availability.

The older signal teaching demo remains opt-in with `EDGEVISION_SIGNAL_DEMO`.
After changing from a build with broken MSVC include dependency detection, do
one clean rebuild before relying on incremental results.

## Lesson 01: letterbox 坐标还原

## 这次解决什么

ORT 预处理把原图等比例缩放后放到补边画布上。旧后处理却按直接拉伸图片的方式分别缩放 x/y，导致非等比例输入的检测框偏移、变形。

例子（框用左上角 x/y 和宽/高表示）：

- 原图 1280×720，模型输入 640×640。
- 比例 r=0.5，内容 640×360，左侧补边 0，顶部补边 140。
- 原图框 (100,100,200,100) 对应模型框 (50,190,100,50)。
- 模型实际输出中心/尺寸 (cx=100,cy=215,w=100,h=50)。
- 旧计算得到约 (100,213,200,56)。
- 正确计算先减 padding，再除 r，得到 (100,100,200,100)。

`restoreLetterboxBox()` 使用预处理已有的 r、padLeft、padTop。整数 resize 会产生取整，当前保持现有预处理与名义缩放比例约定，不在这次修改中调整模型预处理。

边界裁剪分别处理左、上、右、下端点；完全在 padding 或图像外的框直接丢弃。浮点框向外取整为整数框，最多扩张不到一个像素/边。

## 阅读顺序

1. `src/widgets/object_detection_tab_widget.cpp`：`onApplyClicked()` 设置开关并发出配置变化信号；`connectSignals()` 把信号连接到执行回调。
2. `src/ui/mainwindow.cpp`：`processAndDisplay()` 提交图像/配置；`setupResultHandler()` 把调度器完成信号连接到结果处理器。
3. `src/core/pipeline_executor.cpp`：`execute()` 在工作线程完成预处理与目标检测。结果处理器核验图片/ROI/配置归属后只负责显示。
4. `src/algorithm/object_detection_service.cpp`：每线程持有 ORT/DNN 模型，读取本次任务参数；模型加载和推理失败明确返回失败。
5. `src/algorithm/ort_inference.cpp`：resize + padding → float/RGB/CHW → Session::Run → 中心坐标解码 → 本次修复的坐标还原 → NMS。
6. `include/algorithm/letterbox_geometry.h`：单独阅读数学变换，再看测试中的固定数值。

## 运行测试

在仓库根目录的 **x64 Native Tools Command Prompt for VS 2022** 中运行：

```bat
cmake -S tests -B build/geometry-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM=F:/IDE/Qt/Tools/Ninja/ninja.exe
cmake --build build/geometry-tests
ctest --test-dir build/geometry-tests --output-on-failure
build\geometry-tests\letterbox_geometry_test.exe
```

letterbox_geometry 目标只使用仓库内的 OpenCV 头文件，不依赖 Qt、模型或 GPU。13 个场景覆盖宽/高图、方图、非方形输入、奇数补边、四边裁剪、padding 内框、图外框、负尺寸、NaN、无效比例和小数取整。

测试通过证明坐标函数符合这些几何约定；不证明模型准确率、NMS 语义或 GUI 全流程正确。还应使用实际非方形图片做画框验收。

## 自测理解

720×1280 的图像输入 640×640 模型，哪两侧会补边？每侧补多少？为什么要先减补边再除缩放比例？

答案：内容 360×640，左右各 140；模型坐标包含补边产生的平移，需要先撤销平移，再撤销缩放。

## Production UI interactions

Run `test-ui-msvc.cmd` from the repository root. It builds the executable and
`ui_interaction_test` from the same `EdgeVisionRuntime` library, then exercises
real widgets on the Qt offscreen platform. See `docs/ui-interactions.md` for
signal ownership and rendering rules. No camera or broker is required.
