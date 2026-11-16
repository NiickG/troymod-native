@echo off
rem Builds the 32-bit DLL (Rome 2 is 32-bit) and the offline scan test. Static CRT, no extra runtime DLL.
call "N:\Visual Studio 2022\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++17 /O2 /MT /LD /EHsc troymod_native.cpp /Fe:troymod_native.dll /link /NOLOGO || exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc scantest.cpp /Fe:scantest.exe || exit /b 1
