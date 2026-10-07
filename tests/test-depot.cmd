@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client /I deps\json\single_include\nlohmann tests\depot_store.cpp /Fe:build\depot_store_test.exe /Fo:build\depot_store_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\depot_store_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /MT /I deps\lua tests\depot_lui.cpp build\bin\x64\Release\lua.lib /Fe:build\depot_lui_test.exe /Fo:build\depot_lui_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\depot_lui_test.exe
exit /b %errorlevel%
