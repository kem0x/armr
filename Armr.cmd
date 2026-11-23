@echo off
set EXE=%~dp0native-runtime\win\build\aarch64\ArmrLauncher.exe
if not exist "%EXE%" (
  echo ArmrLauncher.exe is not built yet. Run windows\Build-ArmrAarch64.ps1 first.
  pause
  exit /b 1
)
"%EXE%" %*
