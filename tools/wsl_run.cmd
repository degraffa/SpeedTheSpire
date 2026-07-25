@echo off
rem Windows-side wrapper for tools/wsl_run.sh, for callers that are not
rem Git-Bash (cmd.exe, PowerShell). Same arguments as wsl_run.sh:
rem
rem   tools\wsl_run.cmd debug asan release
rem   tools\wsl_run.cmd --script tools/bench_ab.sh a.bin b.bin
rem
rem wslpath does the D:\... -> /mnt/d/... translation inside WSL, so no path
rem string is ever built by hand on this side.
setlocal
if "%STS_WSL_DISTRO%"=="" set "STS_WSL_DISTRO=Ubuntu-2404"
for /f "usebackq delims=" %%p in (`wsl -d %STS_WSL_DISTRO% -e wslpath -a "%~dp0wsl_run.sh"`) do set "SH=%%p"
if "%SH%"=="" echo wsl_run.cmd: could not locate wsl_run.sh inside WSL & exit /b 2
wsl -d %STS_WSL_DISTRO% -- bash "%SH%" %*
exit /b %ERRORLEVEL%
