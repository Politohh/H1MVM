@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\common /I src\client tests\hook_near_memory.cpp /Fe:build\hook_near_memory.exe /Fo:build\hook_near_memory.obj
if errorlevel 1 exit /b 1
build\hook_near_memory.exe
exit /b %errorlevel%
