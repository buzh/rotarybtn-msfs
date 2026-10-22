@echo off
setlocal EnableDelayedExpansion

:: ---------------------------------------------------------------
:: SimConnect SDK path — change this if yours is somewhere else.
:: To find it: search your disk for "SimConnect.h"
:: Typical locations after enabling Developer Mode in MSFS:
::   C:\MSFS SDK\SimConnect SDK\
::   C:\Program Files\Microsoft Flight Simulator\SDK\SimConnect SDK\
:: ---------------------------------------------------------------
set "SDK=C:\MSFS SDK\SimConnect SDK"

if not exist "%SDK%\include\SimConnect.h" (
    echo ERROR: SimConnect SDK not found at:
    echo   %SDK%
    echo.
    echo Enable Developer Mode in MSFS, then install the SDK via:
    echo   Options ^> General ^> Developer Mode ^> Tools ^> SDK Installer
    echo Then update the SDK path in build.bat.
    exit /b 1
)

:: --- Find MSVC ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio not found.
    echo Install "Build Tools for Visual Studio" from:
    echo   https://visualstudio.microsoft.com/downloads/
    echo Make sure to select "Desktop development with C++"
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (
    `"%VSWHERE%" -latest -products * -property installationPath`
) do set "VS=%%i"

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

:: --- Compile ---
echo Building connector.exe ...
cl /nologo /W3 /O2 /TP ^
    connector_gui.c ^
    /I "%SDK%\include" ^
    /link "%SDK%\lib\SimConnect.lib" user32.lib gdi32.lib ^
    /subsystem:windows /out:connector.exe

if %ERRORLEVEL% == 0 (
    echo.
    echo Build OK: connector.exe
) else (
    echo.
    echo Build FAILED.
)
