@echo off
rem Resolve the compiler without changing the system PATH.
if defined NSIS_ROOT (
  if exist "%NSIS_ROOT%\Bin\makensis.exe" (
    set "NSIS_COMPILER=%NSIS_ROOT%\Bin\makensis.exe"
    exit /b 0
  )
  if exist "%NSIS_ROOT%\makensis.exe" (
    set "NSIS_COMPILER=%NSIS_ROOT%\makensis.exe"
    exit /b 0
  )
  echo Error: NSIS_ROOT does not contain makensis.exe.
  exit /b 1
)
for /f "tokens=2,*" %%A in ('reg query HKLM\SOFTWARE\NSIS /ve /reg:32 2^>nul') do (
  if "%%A"=="REG_SZ" set "NSIS_ROOT=%%B"
)
if defined NSIS_ROOT goto resolve
for /f "delims=" %%A in ('where makensis.exe 2^>nul') do (
  set "NSIS_COMPILER=%%A"
  exit /b 0
)
set "NSIS_ROOT=%ProgramFiles(x86)%\NSIS"
:resolve
if exist "%NSIS_ROOT%\Bin\makensis.exe" (
  set "NSIS_COMPILER=%NSIS_ROOT%\Bin\makensis.exe"
  exit /b 0
)
echo Error: Install NSIS or set NSIS_ROOT to its installation folder.
exit /b 1
