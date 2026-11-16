@echo off
rem Builds the 32-bit proof DLL (Rome 2 is 32-bit). Static CRT so no extra runtime DLL is needed.
call "N:\Visual Studio 2022\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /MT /LD /EHsc troymod_native.cpp /Fe:troymod_native.dll /link /NOLOGO
