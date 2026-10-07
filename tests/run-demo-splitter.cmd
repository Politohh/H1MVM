@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I src\client /I deps\zlib tests\demo_splitter.cpp build\bin\x64\Release\zlib.lib /Fe:build\demo_splitter.exe /Fo:build\demo_splitter.obj
if errorlevel 1 exit /b 1
build\demo_splitter.exe %*
exit /b %errorlevel%
