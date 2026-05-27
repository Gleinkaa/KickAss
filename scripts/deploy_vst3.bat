@echo off
REM Deploy KickAss VST3 + Standalone to system locations.
REM Run from an elevated shell (right-click → Run as administrator).
REM
REM This replaces JUCE's COPY_PLUGIN_AFTER_BUILD which fails silently when not elevated.

setlocal

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build
set VST3_SRC=%BUILD_DIR%\KickAss_artefacts\Release\VST3\KickAss.vst3
set STANDALONE_SRC=%BUILD_DIR%\KickAss_artefacts\Release\Standalone\KickAss.exe

set VST3_DEST=%CommonProgramFiles%\VST3

if not exist "%VST3_SRC%" (
    echo [ERROR] %VST3_SRC% not found. Build first with: cmake --build build --config Release
    exit /b 1
)

echo Deploying KickAss.vst3 to %VST3_DEST% ...
xcopy /E /I /Y "%VST3_SRC%" "%VST3_DEST%\KickAss.vst3"
if errorlevel 1 (
    echo [ERROR] Copy failed. Re-run from an elevated shell.
    exit /b 1
)

echo.
echo Done.
echo VST3:        %VST3_DEST%\KickAss.vst3
if exist "%STANDALONE_SRC%" (
    echo Standalone:  %STANDALONE_SRC%   (run directly from build artefacts^)
)

endlocal
