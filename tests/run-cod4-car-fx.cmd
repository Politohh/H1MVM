@echo off
setlocal
cd /d "%~dp0.."
if not exist build mkdir build
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_car_fx.cpp /Fe:build\cod4_car_fx.exe /Fo:build\cod4_car_fx.obj
if errorlevel 1 exit /b 1
set "CAR_FIXTURE=%~1"
if not defined CAR_FIXTURE set "CAR_FIXTURE=C:\Games\cod4_game_files\main\demos\car_explo_to_h1.dm_1"
build\cod4_car_fx.exe "%CAR_FIXTURE%"
exit /b %errorlevel%
