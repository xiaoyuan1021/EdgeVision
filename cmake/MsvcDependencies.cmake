# Keep Ninja header dependencies usable with Chinese-only MSVC + UTF-8 output.
# Other compiler languages retain CMake's detected prefix.
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
