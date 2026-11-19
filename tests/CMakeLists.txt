cmake_minimum_required(VERSION 3.16)
project(EdgeVisionTests LANGUAGES CXX)
include(../cmake/MsvcDependencies.cmake)
set(CMAKE_CXX_STANDARD 20)
enable_testing()
add_executable(letterbox_geometry_test letterbox_geometry_test.cpp)
target_include_directories(letterbox_geometry_test PRIVATE
    ../include ../3rdparty/opencv/include/opencv4)
add_test(NAME letterbox_geometry COMMAND letterbox_geometry_test)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC_OPTIONS --no-autoconnection)
add_executable(config_test
    config_test.cpp
    ../src/config/pipeline_config.cpp
    ../src/config/detection_config_types.cpp
    ../src/config/detection_item.cpp
    ../src/config/roi_config.cpp)
target_include_directories(config_test PRIVATE ../include ../3rdparty/opencv/include/opencv4)
target_link_libraries(config_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
if(MSVC)
    target_compile_options(config_test PRIVATE /utf-8)
endif()
add_test(NAME config COMMAND config_test)
add_executable(inspection_report_test inspection_report_test.cpp
    ../src/data/inspection_report_exporter.cpp)
target_include_directories(inspection_report_test PRIVATE ../include ../3rdparty/opencv/include/opencv4)
target_link_libraries(inspection_report_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
if(MSVC)
    target_compile_options(inspection_report_test PRIVATE /utf-8)
endif()
add_test(NAME inspection_report COMMAND inspection_report_test)
if(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_test_path "${_qt_bin};${_compiler_bin};$ENV{PATH}")
    string(REPLACE ";" "\\;" _test_path "${_test_path}")
    set_tests_properties(config letterbox_geometry inspection_report PROPERTIES ENVIRONMENT "PATH=${_test_path}")
endif()

option(EDGEVISION_SIGNAL_DEMO "Build the Qt configuration signal teaching experiment" OFF)
if(EDGEVISION_SIGNAL_DEMO)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    add_executable(config_signal_demo config_signal_demo.cpp)
    target_link_libraries(config_signal_demo PRIVATE Qt6::Widgets)
    add_test(NAME config_signal_demo COMMAND config_signal_demo -platform offscreen)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
add_executable(pipeline_runtime_test
    pipeline_runtime_test.cpp
    ../src/core/pipeline.cpp
    ../src/core/pipeline_scheduler.cpp
    ../include/core/pipeline_scheduler.h)
target_include_directories(pipeline_runtime_test PRIVATE
    ../include ../include/core ../include/config ../include/data
    ../3rdparty/opencv/include/opencv4 ../3rdparty/spdlog/include)
target_compile_definitions(pipeline_runtime_test PRIVATE SPDLOG_USE_STD_FORMAT)
target_link_libraries(pipeline_runtime_test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Test
    debug "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/lib/opencv_core4d.lib"
    optimized "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/release/lib/opencv_core4.lib")
if(MSVC)
    target_compile_options(pipeline_runtime_test PRIVATE /utf-8)
endif()
add_test(NAME pipeline_runtime COMMAND pipeline_runtime_test)
if(WIN32)
    # Locate only the Debug runtime already supplied on PATH. Linking still uses
    # vendored import libraries; prioritize this runtime over unrelated Python DLLs.
    find_file(_opencv_debug_dll opencv_core4d.dll PATHS ENV PATH NO_DEFAULT_PATH)
    if(_opencv_debug_dll)
        get_filename_component(_opencv_debug_bin "${_opencv_debug_dll}" DIRECTORY)
    endif()
    set(_runtime_path "${_qt_bin};${_compiler_bin};${_opencv_debug_bin};${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/release/bin;$ENV{PATH}")
    string(REPLACE ";" "\\;" _runtime_path "${_runtime_path}")
    set_tests_properties(pipeline_runtime PROPERTIES ENVIRONMENT "PATH=${_runtime_path}")
endif()

add_executable(logger_test logger_test.cpp ../src/core/logger.cpp)
target_include_directories(logger_test PRIVATE ../include ../include/core ../3rdparty/spdlog/include)
target_compile_definitions(logger_test PRIVATE SPDLOG_USE_STD_FORMAT)
target_link_libraries(logger_test PRIVATE Qt6::Widgets Qt6::Test)
if(MSVC)
    target_compile_options(logger_test PRIVATE /utf-8)
endif()
add_test(NAME logger COMMAND logger_test -platform offscreen)
if(WIN32)
    set_tests_properties(logger PROPERTIES ENVIRONMENT "PATH=${_test_path}")
endif()

add_executable(refactor_contract_test
    refactor_contract_test.cpp
    ../src/widgets/barcode_tab_widget.cpp
    ../include/widgets/barcode_tab_widget.h
    ../src/algorithm/detection_evaluator.cpp
    ../src/config/pipeline_config.cpp
    ../src/config/detection_config_types.cpp)
set_target_properties(refactor_contract_test PROPERTIES
    AUTOUIC ON AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../ui/tabs")
target_include_directories(refactor_contract_test PRIVATE
    ../include ../include/core ../include/config ../include/data
    ../3rdparty/opencv/include/opencv4 ../3rdparty/spdlog/include)
target_compile_definitions(refactor_contract_test PRIVATE SPDLOG_USE_STD_FORMAT)
target_link_libraries(refactor_contract_test PRIVATE Qt6::Widgets Qt6::Test
    debug "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/lib/opencv_core4d.lib"
    optimized "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/release/lib/opencv_core4.lib")
if(MSVC)
    target_compile_options(refactor_contract_test PRIVATE /utf-8)
endif()
add_test(NAME refactor_contract COMMAND refactor_contract_test -platform offscreen)
if(WIN32)
    set_tests_properties(refactor_contract PROPERTIES ENVIRONMENT "PATH=${_runtime_path}")
endif()

add_executable(object_detection_service_test
    object_detection_service_test.cpp
    ../src/algorithm/object_detection_service.cpp
    ../src/algorithm/dnn_inference.cpp
    ../src/algorithm/ort_inference.cpp)
target_include_directories(object_detection_service_test PRIVATE
    ../include ../include/algorithm ../3rdparty/opencv/include/opencv4
    ../3rdparty/onnxruntime/include ../3rdparty/spdlog/include)
target_compile_definitions(object_detection_service_test PRIVATE
    SPDLOG_USE_STD_FORMAT PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(object_detection_service_test PRIVATE Qt6::Core Qt6::Test
    "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/onnxruntime/lib/onnxruntime.lib")
foreach(module core imgproc dnn)
    target_link_libraries(object_detection_service_test PRIVATE
        debug "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/lib/opencv_${module}4d.lib"
        optimized "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/release/lib/opencv_${module}4.lib")
endforeach()
if(MSVC)
    target_compile_options(object_detection_service_test PRIVATE /utf-8)
endif()
add_test(NAME object_detection_service COMMAND object_detection_service_test)
set_tests_properties(object_detection_service PROPERTIES TIMEOUT 180)
if(WIN32)
    set(_service_path "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/onnxruntime/lib;${_qt_bin};${_compiler_bin};${_opencv_debug_bin};${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/opencv/release/bin;$ENV{PATH}")
    string(REPLACE ";" "\\;" _service_path "${_service_path}")
    set_tests_properties(object_detection_service PROPERTIES ENVIRONMENT "PATH=${_service_path}")
endif()

if(WIN32)
    foreach(dll onnxruntime onnxruntime_providers_shared onnxruntime_providers_cuda)
        add_custom_command(TARGET object_detection_service_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/onnxruntime/lib/${dll}.dll"
                $<TARGET_FILE_DIR:object_detection_service_test>)
    endforeach()
endif()
