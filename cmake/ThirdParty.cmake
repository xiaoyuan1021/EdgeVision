# ============================================================
# 第三方库配置 (OpenCV)
# ============================================================
set(OPENCV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv")
set(OPENCV_DEBUG_LIB_DIR "${OPENCV_ROOT}/lib")
set(OPENCV_RELEASE_LIB_DIR "${OPENCV_ROOT}/release/lib")
set(OPENCV_RELEASE_BIN_DIR "${OPENCV_ROOT}/release/bin")

# OpenCV 包含路径
target_include_directories(EdgeVisionRuntime PUBLIC
    "${OPENCV_ROOT}/include/opencv4"
    "${OPENCV_ROOT}/include/opencv4/opencv2/ximgproc"
)

# OpenCV 模块列表
set(OPENCV_MODULES
    core imgcodecs highgui imgproc videoio dnn photo
    ximgproc line_descriptor features2d calib3d flann video
)

# Debug 链 opencv_xxx4d.lib，Release 链 opencv_xxx4.lib
foreach(MODULE ${OPENCV_MODULES})
    target_link_libraries(EdgeVisionRuntime PUBLIC
        debug "${OPENCV_DEBUG_LIB_DIR}/opencv_${MODULE}4d.lib"
        optimized "${OPENCV_RELEASE_LIB_DIR}/opencv_${MODULE}4.lib"
    )
endforeach()

# 收集 Release DLL 路径
set(OPENCV_RELEASE_DLLS)
foreach(MODULE ${OPENCV_MODULES})
    list(APPEND OPENCV_RELEASE_DLLS "${OPENCV_RELEASE_BIN_DIR}/opencv_${MODULE}4.dll")
endforeach()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
# 拷贝 Release DLL 到输出目录
add_custom_command(TARGET EdgeVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENCV_RELEASE_DLLS}
        $<TARGET_FILE_DIR:EdgeVision>
    COMMENT "Copying OpenCV Release DLLs..."
)

endif()

# OpenCV 传递依赖 DLL (仅 Release 拷贝，Debug 从 PATH 加载)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET EdgeVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENCV_RELEASE_BIN_DIR}/zlib1.dll"
            "${OPENCV_RELEASE_BIN_DIR}/jpeg62.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libwebp.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libwebpdecoder.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libwebpdemux.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libwebpmux.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libpng16.dll"
            "${OPENCV_RELEASE_BIN_DIR}/tiff.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libsharpyuv.dll"
            "${OPENCV_RELEASE_BIN_DIR}/liblzma.dll"
            "${OPENCV_RELEASE_BIN_DIR}/libprotobuf.dll"
            "${OPENCV_RELEASE_BIN_DIR}/abseil_dll.dll"
            "${OPENCV_RELEASE_BIN_DIR}/lz4.dll"
            "${OPENCV_RELEASE_BIN_DIR}/zstd.dll"
            "${OPENCV_RELEASE_BIN_DIR}/bz2.dll"
            $<TARGET_FILE_DIR:EdgeVision>
        COMMENT "Copying OpenCV vcpkg dependency DLLs..."
    )
endif()


# ============================================================
# 第三方库配置 (ONNX Runtime GPU)
# ============================================================
set(ONNXRT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/onnxruntime")

target_include_directories(EdgeVisionRuntime PUBLIC
    "${ONNXRT_ROOT}/include"
)

target_link_libraries(EdgeVisionRuntime PUBLIC
    debug "${ONNXRT_ROOT}/lib/onnxruntime.lib"
    optimized "${ONNXRT_ROOT}/lib/onnxruntime.lib"
)

# 拷贝 ONNX Runtime DLL 到输出目录
add_custom_command(TARGET EdgeVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRT_ROOT}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:EdgeVision>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRT_ROOT}/lib/onnxruntime_providers_cuda.dll"
        $<TARGET_FILE_DIR:EdgeVision>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRT_ROOT}/lib/onnxruntime_providers_shared.dll"
        $<TARGET_FILE_DIR:EdgeVision>
    COMMENT "Copying ONNX Runtime DLLs..."
)

# ============================================================
# 第三方库配置 (spdlog, header-only)
# ============================================================
target_include_directories(EdgeVisionRuntime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/spdlog/include"
)
target_compile_definitions(EdgeVisionRuntime PUBLIC SPDLOG_USE_STD_FORMAT)

# =========================
# ZXing
# =========================

set(ZXING_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/zxing")

target_include_directories(EdgeVisionRuntime PUBLIC
    "${ZXING_ROOT}/include"
)

target_link_libraries(EdgeVisionRuntime PUBLIC
    debug "${ZXING_ROOT}/lib/ZXingd.lib"
    optimized "${ZXING_ROOT}/release/lib/ZXing.lib"
)


# ============================================================
# 第三方库配置 (RapidOCR - PP-OCRv4 ONNX)
# ============================================================
set(RAPIDOCR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rapidocr")

target_include_directories(EdgeVisionRuntime PUBLIC
    "${RAPIDOCR_ROOT}/include"
)

set(RAPIDOCR_SOURCES
    ${RAPIDOCR_ROOT}/src/OcrLite.cpp
    ${RAPIDOCR_ROOT}/src/OcrLiteImpl.cpp
    ${RAPIDOCR_ROOT}/src/DbNet.cpp
    ${RAPIDOCR_ROOT}/src/AngleNet.cpp
    ${RAPIDOCR_ROOT}/src/CrnnNet.cpp
    ${RAPIDOCR_ROOT}/src/OcrUtils.cpp
    ${RAPIDOCR_ROOT}/src/clipper.cpp
)

target_sources(EdgeVisionRuntime PRIVATE ${RAPIDOCR_SOURCES})

# 第三方代码禁用特定警告（含系统头文件中的警告）
if(MSVC)
    set_source_files_properties(${RAPIDOCR_SOURCES}
        PROPERTIES COMPILE_FLAGS "/wd4244 /wd4267 /wd4305 /wd4477 /wd4996"
    )
endif()


# ============================================================
# 第三方库配置 (Paho MQTT)
# ============================================================
set(PAHO_MQTT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/paho-mqtt")

target_include_directories(EdgeVisionRuntime PUBLIC
    "${PAHO_MQTT_ROOT}/include"
)

# 必须定义导入宏，否则 MSVC 无法解析 DLL 中的数据符号（如 mqtt::message::EMPTY_STR）
target_compile_definitions(EdgeVisionRuntime PUBLIC
    PAHO_MQTTPP_IMPORTS
    PAHO_MQTT_IMPORTS
)

# Paho MQTT C++ wrapper + C core
target_link_libraries(EdgeVisionRuntime PUBLIC
    debug "${PAHO_MQTT_ROOT}/lib/paho-mqttpp3d.lib"
    optimized "${PAHO_MQTT_ROOT}/release/lib/paho-mqttpp3.lib"
    debug "${PAHO_MQTT_ROOT}/lib/paho-mqtt3asd.lib"
    optimized "${PAHO_MQTT_ROOT}/release/lib/paho-mqtt3as.lib"
)

# 拷贝 Paho MQTT + OpenSSL DLL (仅 Release，Debug 通过 PATH 加载)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET EdgeVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAHO_MQTT_ROOT}/release/bin/paho-mqttpp3.dll"
            $<TARGET_FILE_DIR:EdgeVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAHO_MQTT_ROOT}/release/bin/paho-mqtt3as.dll"
            $<TARGET_FILE_DIR:EdgeVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAHO_MQTT_ROOT}/release/bin/libssl-3-x64.dll"
            $<TARGET_FILE_DIR:EdgeVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAHO_MQTT_ROOT}/release/bin/libcrypto-3-x64.dll"
            $<TARGET_FILE_DIR:EdgeVision>
        COMMENT "Copying Paho MQTT + OpenSSL DLLs..."
    )
endif()
