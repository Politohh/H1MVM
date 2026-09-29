@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_promod_stress.cpp /Fe:build\cod4_promod_stress.exe /Fo:build\cod4_promod_stress.obj
if errorlevel 1 exit /b 1
build\cod4_promod_stress.exe "C:\Games\cod4_game_files\Mods\promodlive204\demos\dEbilzuuR_2k_deag_r700_crash.dm_1" "build\analysis\h1-city-actor-codecs.txt"
exit /b %errorlevel%
