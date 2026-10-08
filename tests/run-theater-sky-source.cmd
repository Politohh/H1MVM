@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client /I deps\json\single_include\nlohmann tests\theater_sky_source.cpp /Fe:build\theater_sky_source.exe /Fo:build\theater_sky_source.obj
if errorlevel 1 exit /b 1
build\theater_sky_source.exe %*
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_sky_gpu.cpp d3d11.lib /Fe:build\theater_sky_gpu.exe /Fo:build\theater_sky_gpu.obj
if errorlevel 1 exit /b 1
build\theater_sky_gpu.exe %3
exit /b %errorlevel%
