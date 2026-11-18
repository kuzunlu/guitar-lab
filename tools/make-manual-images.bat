@echo off
rem Makes the pictures of the user manual (docs\manual\img) from the real plugin window, off-screen.
rem Run build.bat once first (it downloads JUCE into build\_deps). Run this again before every release.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto noVisualStudio
set "VS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS goto noVisualStudio

cd /d "%~dp0.."
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "VSCMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if exist "%VSCMAKE%\CMake\bin\cmake.exe" set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%PATH%"

if not exist "build\_deps\juce-src" (
    echo Run build.bat first: it downloads JUCE.
    exit /b 1
)

cmake -S . -B build-manual -G Ninja -DCMAKE_BUILD_TYPE=Release -DGUITAR_LAB_SCREENSHOTS=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=%CD%\build\_deps\juce-src" || exit /b 1
cmake --build build-manual --target GuitarLabScreenshots || exit /b 1
if not exist "docs\manual\img" mkdir "docs\manual\img"
"build-manual\GuitarLabScreenshots_artefacts\Release\GuitarLabScreenshots.exe" "%CD%\docs\manual\img" || exit /b 1
echo.
echo Pictures written to docs\manual\img
exit /b 0

:noVisualStudio
echo Visual Studio (or Build Tools) with the C++ workload was not found.
exit /b 1
