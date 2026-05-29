@echo off
REM Build the KickAss Windows installer with Inno Setup 6.
REM Prereq: build the Release artefacts first:
REM     cmake --build build --config Release --target KickAss_All
REM Output: installer\Output\KickAss-<version>-Setup.exe

setlocal

REM Resolve project root (this script lives in scripts\).
set "ROOT=%~dp0.."
set "ISS=%ROOT%\installer\KickAss.iss"

REM Locate ISCC.exe (Inno Setup compiler). Adjust if installed elsewhere.
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"

if not exist "%ISCC%" (
    echo [ERROR] ISCC.exe not found. Install Inno Setup 6 from https://jrsoftware.org/isdl.php
    echo         or set ISCC to its full path at the top of this script.
    exit /b 1
)

if not exist "%ROOT%\build\KickAss_artefacts\Release\Standalone\KickAss.exe" (
    echo [ERROR] Release artefacts not found. Build first:
    echo         cmake --build build --config Release --target KickAss_All
    exit /b 1
)

echo Compiling installer with "%ISCC%" ...
"%ISCC%" "%ISS%"
if errorlevel 1 (
    echo [ERROR] Inno Setup compilation failed.
    exit /b 1
)

echo.
echo Installer written to installer\Output\
endlocal
