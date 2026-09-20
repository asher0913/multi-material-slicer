@echo off
REM =====================================================================
REM  MultiMaterialSlicer - Windows one-click build / package
REM  Double-click this file to build everything into dist\.
REM
REM  Prerequisites (install once):
REM    - Qt 5.12.12 MSVC 64-bit   (e.g. C:\Qt\5.12.12\msvc2017_64)
REM    - Visual Studio 2019/2022 with "Desktop development with C++"
REM    - CMake                    (https://cmake.org, add to PATH)
REM    - Python 3.10/3.11/3.12    (CadQuery/OCP STEP import needs these wheels)
REM =====================================================================
setlocal
echo ============================================================
echo   MultiMaterialSlicer - Windows one-click build
echo ============================================================
echo.

REM Run the PowerShell packaging script, bypassing the execution policy
REM so the user does not have to change any system settings.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_windows.ps1" %*
set ERR=%ERRORLEVEL%

echo.
if "%ERR%"=="0" (
    echo ============================================================
    echo   BUILD SUCCEEDED
    echo   Output: dist\MultiMaterialSlicer-win64.zip
    echo ============================================================
) else (
    echo ============================================================
    echo   BUILD FAILED  ^(exit code %ERR%^)
    echo   Read the messages above for the missing prerequisite.
    echo ============================================================
)
echo.
echo Press any key to close this window...
pause >nul
endlocal
