@echo off
setlocal
set "EH_ROOT=%~dp0"
set "EH_ROOT=%EH_ROOT:~0,-1%"
set "EH_CMAKE=cmake"

where cmake >nul 2>nul
if errorlevel 1 (
  if exist "F:\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "EH_CMAKE=F:\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  ) else (
    echo CMake was not found. Install CMake 3.22 or newer and add it to PATH.
    exit /b 1
  )
)

set "EH_JUCE_OPTION="
if exist "F:\JUCE\CMakeLists.txt" set "EH_JUCE_OPTION=-DEVENT_HORIZON_JUCE_PATH=F:/JUCE"

"%EH_CMAKE%" -S "%EH_ROOT%" -B "%EH_ROOT%\build-release" %EH_JUCE_OPTION%
if errorlevel 1 exit /b %errorlevel%

"%EH_CMAKE%" --build "%EH_ROOT%\build-release" --config Release --parallel 4
exit /b %errorlevel%
