@echo off
setlocal
cd /d "%~dp0.."
if "%~1"=="" (
    echo Usage: %~nx0 "path\to\D1ablo_cic7_infinite.dm_1"
    exit /b 1
)
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this test from an x64 Native Tools Command Prompt for Visual Studio.
    exit /b 1
)
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_vehicle_variants.cpp /Fe:build\cod4_vehicle_variants.exe /Fo:build\cod4_vehicle_variants.obj
if errorlevel 1 exit /b 1
build\cod4_vehicle_variants.exe "%~1"
exit /b %errorlevel%
