@echo off

set VcvarsallPath="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set CommomCompilerFlags=-MT -nologo -Gm- -GR- -EHa- -Od -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -DINTERNAL=1 -DSLOW_PERFORMANCE=1 -Z7 -FC -Fmwin32_handmade.map
set CommonLinkerFlags=-opt:ref user32.lib gdi32.lib winmm.lib

IF NOT EXIST ..\..\build mkdir ..\..\build
pushd ..\..\build

REM 32 Bit Build
REM call %VcvarsallPath% x86
REM cl %CommonCompilerFlags% ..\handmade\code\win32_handmade.cpp /link -subsystem:windows,5.1 %CommonLinkerFlags%

REM 64 Bit Build
call %VcvarsallPath% amd64
cl %CommomCompilerFlags% ..\handmade\code\win32_handmade.cpp /link %CommonLinkerFlags%

popd
