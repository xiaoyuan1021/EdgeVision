@echo off
setlocal
cd /d "%~dp0"
if not defined EDGEVISION_BUILD_DIR set "EDGEVISION_BUILD_DIR=build/msvc-buildtools-debug-v2"
set "EDGEVISION_UI_TESTS=ON"
call "%~dp0build-msvc.cmd"
if errorlevel 1 exit /b 1
call "%~dp0scripts\msvc-env.cmd"
if errorlevel 1 exit /b 1
set "PATH=%EDGEVISION_QT_DIR%\bin;%~dp03rdparty\paho-mqtt\bin;%PATH%"
ctest --test-dir "%EDGEVISION_BUILD_DIR%" -R "^(ui_interaction|application_workflow|image_view|inspection_results)$" --output-on-failure
set "TEST_EXIT=%errorlevel%"
if not "%TEST_EXIT%"=="0" type "%EDGEVISION_BUILD_DIR%\ui_interaction-results.txt"
if not "%TEST_EXIT%"=="0" type "%EDGEVISION_BUILD_DIR%\application_workflow-results.txt"
if not "%TEST_EXIT%"=="0" type "%EDGEVISION_BUILD_DIR%\image_view-results.txt"
if not "%TEST_EXIT%"=="0" type "%EDGEVISION_BUILD_DIR%\inspection_results-results.txt"
exit /b %TEST_EXIT%
