@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_hud.cpp /Fe:build\cod4_hud.exe /Fo:build\cod4_hud.obj
if errorlevel 1 exit /b 1
build\cod4_hud.exe "F:\CLIPS\Polito Best Clips All Games\TDM_PROMOD_STRIKE_5k_r700_SICK_NOSC_SKY.dm_1"
exit /b %errorlevel%
