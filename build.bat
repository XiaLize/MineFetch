@echo off
setlocal

if exist "C:\mingw64\bin\g++.exe" (
    set "PATH=C:\mingw64\bin;%PATH%"
)

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ was not found.
    echo Install MinGW-w64 at C:\mingw64, or add its bin directory to PATH.
    exit /b 1
)

if "%NUMBER_OF_PROCESSORS%"=="" set "NUMBER_OF_PROCESSORS=4"

mingw32-make -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo Build complete: minefetch.exe
