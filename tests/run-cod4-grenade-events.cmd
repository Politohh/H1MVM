@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_grenade_events.cpp /Fo:build\cod4_grenade_events.obj /Fe:build\cod4_grenade_events.exe
if errorlevel 1 exit /b 1
if "%~1"=="" (
    build\cod4_grenade_events.exe "C:\Games\cod4_game_files\main\demos\cod4_to_H1.dm_1" "C:\Games\cod4_game_files\main\demos\backlot_cod4_to_h1.dm_1" "C:\Games\cod4_game_files\main\demos\city_cod4_to_h1_test_player_____.dm_1"
) else (
    build\cod4_grenade_events.exe %*
)
exit /b %errorlevel%
