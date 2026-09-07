@echo off
rem Keep this entrypoint ASCII; use the same helper from cmd or PowerShell.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0win_build.ps1" %*
exit /b %ERRORLEVEL%
