@echo off
rem Run once per terminal session:  env.bat
set "ROOT=%~dp0"
set "VCPKG_ROOT=%ROOT%tools\vcpkg"
set "PATH=%ROOT%tools\mingw64\bin;%ROOT%tools\cmake\bin;%PATH%"
