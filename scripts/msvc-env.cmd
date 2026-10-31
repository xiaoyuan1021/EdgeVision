@echo off
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Installer / vswhere not found.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo Install the Desktop development with C++ workload first.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not defined EDGEVISION_QT_DIR set "EDGEVISION_QT_DIR=F:\IDE\Qt\6.9.1\msvc2022_64"
if not defined EDGEVISION_NINJA set "EDGEVISION_NINJA=F:\IDE\Qt\Tools\Ninja\ninja.exe"
exit /b 0
