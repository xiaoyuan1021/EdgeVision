@echo off
setlocal
cd /d "%~dp0"
call "%~dp0scripts\msvc-env.cmd"
if errorlevel 1 exit /b 1
if not defined EDGEVISION_TEST_DIR set "EDGEVISION_TEST_DIR=build/refactor-tests"
cmake -S tests -B "%EDGEVISION_TEST_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_MAKE_PROGRAM=%EDGEVISION_NINJA%" "-DCMAKE_PREFIX_PATH=%EDGEVISION_QT_DIR%"
if errorlevel 1 exit /b 1
cmake --build "%EDGEVISION_TEST_DIR%" --parallel 4
if errorlevel 1 exit /b 1
ctest --test-dir "%EDGEVISION_TEST_DIR%" --output-on-failure
exit /b %errorlevel%
