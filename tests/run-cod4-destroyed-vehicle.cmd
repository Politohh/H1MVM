@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_destroyed_vehicle.cpp /Fo:build\cod4_destroyed_vehicle.obj /Fe:build\cod4_destroyed_vehicle.exe
if errorlevel 1 exit /b 1
if "%~1"=="" (
    build\cod4_destroyed_vehicle.exe "C:\Games\cod4_game_files\main\demos\car_explo_to_h1.dm_1"
) else (
    build\cod4_destroyed_vehicle.exe "%~1"
)
exit /b %errorlevel%
