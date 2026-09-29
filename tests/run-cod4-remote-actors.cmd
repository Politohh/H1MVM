@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_remote_actors.cpp /Fe:build\cod4_remote_actors.exe /Fo:build\cod4_remote_actors.obj
if errorlevel 1 exit /b 1
build\cod4_remote_actors.exe "C:\Games\cod4_game_files\main\demos\city_cod4_to_h1_test_player_____.dm_1" "build\analysis\h1-city-actor-codecs.txt"
exit /b %errorlevel%
