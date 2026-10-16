@echo off
setlocal
cd /d "%~dp0.."
call tests\setup-msvc.cmd
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_library.cpp /Fe:build\theater_library_test.exe /Fo:build\theater_library_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_library_test.exe %*
exit /b %errorlevel%
