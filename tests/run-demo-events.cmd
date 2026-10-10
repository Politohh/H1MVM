@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I src\client tests\demo_events.cpp /Fe:build\demo_events.exe /Fo:build\demo_events.obj
if errorlevel 1 exit /b 1
build\demo_events.exe %*
exit /b %errorlevel%
