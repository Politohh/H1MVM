@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_corpse_identity.cpp /Fe:build\cod4_corpse_identity.exe /Fo:build\cod4_corpse_identity.obj
if errorlevel 1 exit /b 1
build\cod4_corpse_identity.exe "%~1"
exit /b %errorlevel%
