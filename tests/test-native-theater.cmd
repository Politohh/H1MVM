@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\demo_timeline.cpp /Fe:build\demo_timeline_test.exe /Fo:build\demo_timeline_test.obj
if errorlevel 1 exit /b 1
build\demo_timeline_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /I deps\lua tests\theater_lui.cpp build\bin\x64\Release\lua.lib /Fe:build\theater_lui_test.exe /Fo:build\theater_lui_test.obj
if errorlevel 1 exit /b 1
build\theater_lui_test.exe
exit /b %errorlevel%
