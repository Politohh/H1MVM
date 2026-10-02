@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_transient_budget.cpp /Fe:build\cod4_transient_budget.exe /Fo:build\cod4_transient_budget.obj
if errorlevel 1 exit /b 1
build\cod4_transient_budget.exe %*
exit /b %errorlevel%
