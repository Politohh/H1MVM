@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_obituary_payload.cpp /Fe:build\cod4_obituary_payload.exe /Fo:build\cod4_obituary_payload.obj
if errorlevel 1 exit /b 1
build\cod4_obituary_payload.exe %*
exit /b %errorlevel%
