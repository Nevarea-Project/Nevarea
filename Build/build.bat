@echo off
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=windows-msvc
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET%
