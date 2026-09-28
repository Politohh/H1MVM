@call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
@cl /nologo /std:c++20 /EHsc /W4 /I src\client work\export-camera-archives.cpp /Fe:build\export-camera-archives.exe /Fo:build\export-camera-archives.obj
@if errorlevel 1 exit /b 1
@build\export-camera-archives.exe "C:\Games\cod4_game_files\main\demos\cod4_to_H1.dm_1" build\camera-archives.bin
