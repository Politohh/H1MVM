@echo off
setlocal
cd /d "%~dp0.."
call tests\setup-msvc.cmd
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_fov.cpp /Fe:build\cod4_fov_test.exe /Fo:build\cod4_fov_test.obj
if errorlevel 1 exit /b 1
build\cod4_fov_test.exe
exit /b %errorlevel%
