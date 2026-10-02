@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_hud_timing.cpp /Fe:build\cod4_hud_timing.exe /Fo:build\cod4_hud_timing.obj
if errorlevel 1 exit /b 1
build\cod4_hud_timing.exe "C:\Users\Paul\Downloads\cic7_fnaticfinal_Crossfire_1_20_Y.dm_1" "C:\Users\Paul\Downloads\D1ablo_cic7_infinite.dm_1"
exit /b %errorlevel%
