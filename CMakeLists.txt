cmake_minimum_required(VERSION 3.16)

project(EdgeVision VERSION 2.0.0 LANGUAGES CXX)

include(cmake/MsvcDependencies.cmake)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CMAKE_CXX_STANDARD 20)
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_OPTIONS --no-autoconnection)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# 设置构建输出目录 (仅在未指定时设置默认值)
if(NOT CMAKE_BINARY_DIR)
    set(CMAKE_BINARY_DIR "${CMAKE_SOURCE_DIR}/build/Debug")
endif()

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 告诉 CMake 的 AUTOUIC 在哪些目录中查找 .ui 文件
list(APPEND CMAKE_AUTOUIC_SEARCH_PATHS 
    ${CMAKE_CURRENT_SOURCE_DIR}/ui 
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/tabs
)

# ============================================================
# 查找 Qt6 库
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# ============================================================
# 包含路径配置
# ============================================================
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/algorithm
    ${CMAKE_CURRENT_SOURCE_DIR}/include/config
    ${CMAKE_CURRENT_SOURCE_DIR}/include/data
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/include/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
)

# ============================================================
# 源文件收集（显式列出，新增文件时需手动添加）
# ============================================================
set(HEADERS
    # application: orchestration without Designer/MainWindow dependencies
    include/application/inspection_session.h
    include/application/detection_workspace.h
    include/application/remote_inspection_controller.h
    include/ui/scoped_widget_signals.h
    # core
    include/core/logger.h
    include/core/mqtt_client.h
    include/core/mqtt_manager.h
    include/core/pipeline_executor.h
    include/core/pipeline.h
    include/core/pipeline_manager.h
    include/core/pipeline_request.h
    include/core/pipeline_result.h
    include/core/pipeline_scheduler.h
    include/core/pipeline_steps.h
    include/core/profile_manager.h
    include/core/roi_manager.h
    include/core/video_manager.h
    include/core/barcode_step.h
    include/core/i_pipeline_access.h
    # algorithm
    include/algorithm/object_detection_service.h
    include/data/object_detection_result.h
    include/algorithm/dnn_inference.h
    include/algorithm/detection_evaluator.h
    include/algorithm/image_processor.h
    include/algorithm/image_utils.h
    include/algorithm/match_strategy.h
    include/algorithm/opencv_algorithm.h
    include/algorithm/ort_inference.h
    include/algorithm/letterbox_geometry.h
    include/algorithm/zxing_barcode_reader.h
    include/algorithm/display_renderer.h
    # config
    include/config/algorithm_step.h
    include/config/config_manager.h
    include/config/constants.h
    include/config/detection_config_types.h
    include/config/detection_item.h
    include/config/detection_type.h
    include/config/display_config.h
    include/config/json_serializable.h
    include/config/mqtt_config.h
    include/config/pipeline_config.h
    include/config/roi_config.h
    include/config/shape_filter_types.h
    include/config/tab_config.h
    # config (独立配置头文件)
    include/config/enhance_config.h
    include/config/color_filter_config.h
    include/config/image_filter_config.h
    include/config/ocr_config.h
    include/config/object_detection_config.h
    include/config/line_detect_config.h
    include/config/blob_judge_config.h
    include/config/barcode_config.h
    # data
    include/data/barcode_result.h
    include/data/detection_result_report.h
    include/data/inspection_profile.h
    include/data/region_feature.h
    # ui
    include/ui/cloud_dashboard_manager.h
    include/ui/display_mode_manager.h
    include/ui/file_manager.h
    include/ui/image_view.h
    include/ui/log_page.h
    include/ui/mainwindow.h
    include/ui/slider_spinbox_binder.h
    include/ui/system_monitor.h
    include/ui/toast_notification.h
    # widgets
    include/widgets/barcode_tab_widget.h
    include/widgets/batch_detection_widget.h
    include/widgets/batch_match_dialog.h
    include/widgets/enhance_tab_widget.h
    include/widgets/extract_tab_widget.h
    include/widgets/filter_tab_widget.h
    include/widgets/i_tab_interfaces.h
    include/widgets/image_tab_widget.h
    include/widgets/judge_tab_widget.h
    include/widgets/line_tab_widget.h
    include/widgets/object_detection_tab_widget.h
    include/widgets/process_tab_widget.h
    include/widgets/profile_tab_widget.h
    include/widgets/roi_detection_config_dialog.h
    include/widgets/tab_manager.h
    include/widgets/tab_registry.h
    include/widgets/template_tab_widget.h
    include/widgets/step_config_widget.h
    include/widgets/video_source_widget.h
    include/widgets/video_tab_widget.h
    include/widgets/image_filter_tab_widget.h
    include/widgets/ocr_tab_widget.h
    include/core/ocr_step.h
    include/data/ocr_region.h
    # controllers
    include/controllers/image_list_manager.h
    include/controllers/auto_detection_controller.h
    include/controllers/config_controller.h
    include/controllers/detection_ui_controller.h
    include/controllers/profile_controller.h
    include/controllers/pipeline_result_handler.h
    include/controllers/roi_ui_controller.h
    include/controllers/roi_detection_config_controller.h
    # utils
    include/utils/path_utils.h
    include/utils/benchmark.h
    include/utils/model_warmup.h
)

set(SOURCES
    # application
    src/application/inspection_session.cpp
    src/application/detection_workspace.cpp
    src/application/remote_inspection_controller.cpp
    # core
    src/core/logger.cpp
    src/core/mqtt_client.cpp
    src/core/mqtt_manager.cpp
    src/core/pipeline_executor.cpp
    src/core/pipeline.cpp
    src/core/pipeline_manager.cpp
    src/core/pipeline_scheduler.cpp
    src/core/pipeline_steps.cpp
    src/core/profile_manager.cpp
    src/core/roi_manager.cpp
    src/core/video_manager.cpp
    src/core/barcode_step.cpp
    src/core/pipeline_image_filter_steps.cpp
    src/core/ocr_step.cpp
    # algorithm
    src/algorithm/object_detection_service.cpp
    src/algorithm/dnn_inference.cpp
    src/algorithm/detection_evaluator.cpp
    src/algorithm/image_processor.cpp
    src/algorithm/image_utils.cpp
    src/algorithm/match_strategy.cpp
    src/algorithm/opencv_algorithm.cpp
    src/algorithm/ort_inference.cpp
    src/algorithm/zxing_barcode_reader.cpp
    src/algorithm/display_renderer.cpp
    # data
    src/data/inspection_profile.cpp
    # config
    src/config/config_manager.cpp
    src/config/pipeline_config.cpp
    src/config/roi_config.cpp
    src/config/detection_item.cpp
    src/config/detection_config_types.cpp
    # ui
    src/ui/cloud_dashboard_manager.cpp
    src/ui/display_mode_manager.cpp
    src/ui/file_manager.cpp
    src/ui/image_view.cpp
    src/ui/log_page.cpp
    src/ui/mainwindow.cpp
    src/ui/system_monitor.cpp
    src/ui/toast_notification.cpp
    # widgets
    src/widgets/barcode_tab_widget.cpp
    src/widgets/batch_detection_widget.cpp
    src/widgets/batch_match_dialog.cpp
    src/widgets/enhance_tab_widget.cpp
    src/widgets/extract_tab_widget.cpp
    src/widgets/filter_tab_widget.cpp
    src/widgets/image_tab_widget.cpp
    src/widgets/judge_tab_widget.cpp
    src/widgets/line_tab_widget.cpp
    src/widgets/object_detection_tab_widget.cpp
    src/widgets/process_tab_widget.cpp
    src/widgets/profile_tab_widget.cpp
    src/widgets/roi_detection_config_dialog.cpp
    src/widgets/tab_manager.cpp
    src/widgets/tab_registry.cpp
    src/widgets/template_tab_widget.cpp
    src/widgets/step_config_widget.cpp
    src/widgets/video_source_widget.cpp
    src/widgets/video_tab_widget.cpp
    src/widgets/image_filter_tab_widget.cpp
    src/widgets/ocr_tab_widget.cpp
    # controllers
    src/controllers/image_list_manager.cpp
    src/controllers/auto_detection_controller.cpp
    src/controllers/config_controller.cpp
    src/controllers/detection_ui_controller.cpp
    src/controllers/pipeline_result_handler.cpp
    src/controllers/profile_controller.cpp
    src/controllers/roi_ui_controller.cpp
    src/controllers/roi_detection_config_controller.cpp
    # utils
    src/utils/model_warmup.cpp
)

set(FORMS
    ui/mainwindow.ui
    ui/home_page.ui
    ui/log_page.ui
)

# 资源文件 (RESOURCES)
set(RESOURCES
    resources/res.qrc
)

# ============================================================
# 创建可执行文件目标
# ============================================================
add_library(EdgeVisionRuntime STATIC
    ${HEADERS}
    ${SOURCES}
    ${FORMS}
)
add_executable(EdgeVision src/main.cpp ${RESOURCES})

# 设置包含路径
target_include_directories(EdgeVisionRuntime PUBLIC ${INCLUDE_DIRS})

# 定义项目根目录宏，供代码使用
target_compile_definitions(EdgeVisionRuntime PUBLIC PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# 链接 Qt 库
target_link_libraries(EdgeVisionRuntime PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Windows 系统库 (PDH 性能监控)
if(WIN32)
    target_link_libraries(EdgeVisionRuntime PUBLIC pdh)
endif()

# Vendored library linkage and runtime deployment.
include(cmake/ThirdParty.cmake)

# ============================================================
# 编译器特定选项 (MSVC)
# ============================================================
if(MSVC)
    # 添加 MSVC 编译选项
    target_compile_options(EdgeVisionRuntime PRIVATE
        /W3   # 警告级别 3
        /EHsc # 启用 C++ 异常处理
        /Zi   # 调试信息格式（Debug/RelWithDebInfo 下有用）
        $<$<CONFIG:Debug>:/MDd>         # Debug: 多线程调试 DLL 运行时
        $<$<CONFIG:Release>:/MD>        # Release: 多线程 DLL 运行时
        $<$<CONFIG:RelWithDebInfo>:/MD> # RelWithDebInfo: 多线程 DLL 运行时
    )
    # 添加链接器选项（仅 Debug 生成完整调试信息）
    target_link_options(EdgeVision PRIVATE
        $<$<CONFIG:Debug>:/DEBUG>
    )
endif()


# ============================================================
# 编译输出配置
# ============================================================
set_target_properties(EdgeVision PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# The executable and Qt interaction tests share the same production objects.
target_link_libraries(EdgeVision PRIVATE EdgeVisionRuntime)
include(cmake/UiTests.cmake)
