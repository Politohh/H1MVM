@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
call tests\run-cod4-fov.cmd
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_dof.cpp /Fe:build\theater_dof_test.exe /Fo:build\theater_dof_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_dof_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_focus.cpp d3d11.lib /Fe:build\theater_focus_test.exe /Fo:build\theater_focus_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_focus_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\demo_timeline.cpp /Fe:build\demo_timeline_test.exe /Fo:build\demo_timeline_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\demo_timeline_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_sky.cpp /Fe:build\theater_sky_test.exe /Fo:build\theater_sky_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_sky_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_scene.cpp /Fe:build\theater_scene_test.exe /Fo:build\theater_scene_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_scene_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client /I deps\json\single_include\nlohmann tests\theater_models.cpp /Fe:build\theater_models_test.exe /Fo:build\theater_models_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_models_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\theater_weapons.cpp /Fe:build\theater_weapons_test.exe /Fo:build\theater_weapons_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_weapons_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client /I deps\json\single_include\nlohmann tests\ui_json_conversion.cpp /Fe:build\ui_json_conversion_test.exe /Fo:build\ui_json_conversion_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\ui_json_conversion_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /MT /I deps\lua tests\theater_lui.cpp build\bin\x64\Release\lua.lib /Fe:build\theater_lui_test.exe /Fo:build\theater_lui_test.obj
if not "%errorlevel%"=="0" exit /b 1
build\theater_lui_test.exe
if not "%errorlevel%"=="0" exit /b 1
build\theater_lui_test.exe tests\startup_lobby.lua
exit /b %errorlevel%
