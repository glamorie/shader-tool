@echo off
setlocal enabledelayedexpansion

:begin-parse
if "%1"==""                goto :run
if "%1"=="-h"              goto :show-help
if "%1"=="-?"              goto :show-help
if "%1"=="--help"          goto :show-help
if "%1"=="-e"              goto :set-entry
if "%1"=="--entry"         goto :set-entry
if "%1"=="-p"              goto :set-psmain
if "%1"=="--psmain"        goto :set-psmain
if "%1"=="-r"              goto :set-rthread
if "%1"=="--render-thread" goto :set-rthread

echo Ignoring [%1]
goto :end-parse

:show-help
echo Usage:
echo   %0 [-?^|-h^|--help]
echo   %0 [-r^|--render-thread] --psmain ^<function-name^> --entry ^<function-name^>
echo.
echo Arguments:
echo    -e,--entry            Set a custom virtual entry point for the shaders. If for whatever reason
echo                          `__PsMain` is used, use `--psmain` to prevent conflicts.
echo    -p,--psmain           Set the name of the entry point
echo.
echo Options:
echo    -r,--render-thread    Enable rendering to a separate thread.
echo    -?,-h,--help          Show this message and exit.
goto :EOF

:set-entry
shift
if "%1"=="" (
  echo Entry was not specified.
  exit /b 1
)

set "__Entry=%1"
goto :end-parse

:set-psmain
shift
if "%1"=="" (
  echo Entry was not specified.
  exit /b 1
)
set "__PsMain=%1"
goto :end-parse

:set-rthread
set "__RenderThread=1"
goto :end-parse

:end-parse
shift
goto :begin-parse

:run

set "__Flags="

where cl.exe /Q || (
  echo Msvc compiler not found! Launch x64 Native Tools Command Prompt and run this script
  exit /b 1
)

if defined __RenderThread set "__Flags=!__Flags! /DShaderToolRenderThread=1"
if defined __Entry set "__Flags=!__Flags! /DShaderToolEntry=%__Entry%"
if defined __PsMain set "__Flags=!__Flags! /DShaderToolPsMain=%__PsMain%"

pushd "%~dp0"

if not exist "build\" mkdir build\
pushd build\
echo Building ...
cl.exe /I.. %__Flags% ..\main.c /Fe:ShaderTool.exe
popd
popd
