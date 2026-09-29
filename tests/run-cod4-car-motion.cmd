@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_car_motion.cpp /Fe:build\cod4_car_motion.exe /Fo:build\cod4_car_motion.obj
if errorlevel 1 exit /b 1
build\cod4_car_motion.exe "C:\Games\cod4_game_files\main\demos\car_explo_to_h1.dm_1"
exit /b %errorlevel%
