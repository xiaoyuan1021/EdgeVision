@echo off
setlocal
set "VSLANG=1033"
cd /d "%~dp0"
call "%~dp0scripts\msvc-env.cmd"
if errorlevel 1 exit /b 1
if not defined EDGEVISION_BUILD_DIR set "EDGEVISION_BUILD_DIR=build/msvc-buildtools-debug-v2"
if not defined EDGEVISION_UI_TESTS set "EDGEVISION_UI_TESTS=OFF"
cmake -S . -B "%EDGEVISION_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DEDGEVISION_UI_TESTS=%EDGEVISION_UI_TESTS% "-DCMAKE_MAKE_PROGRAM=%EDGEVISION_NINJA%" "-DCMAKE_PREFIX_PATH=%EDGEVISION_QT_DIR%"
if errorlevel 1 exit /b 1
cmake --build "%EDGEVISION_BUILD_DIR%" --parallel 4
exit /b %errorlevel%
