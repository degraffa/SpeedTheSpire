@echo off
rem Windows-side wrapper for tools/task_worktree.sh, for callers that are not
rem Git-Bash (cmd.exe, PowerShell). Same arguments as task_worktree.sh:
rem
rem   tools\task_worktree.cmd create <name>
rem   tools\task_worktree.cmd land <name>
rem   tools\task_worktree.cmd list
rem
rem Unlike wsl_run.cmd this must NOT go through WSL, and in particular must not
rem use the bare `bash` on the PowerShell PATH -- that one is WSL's bash, whose
rem git cannot read a linked worktree's `gitdir: D:/...` file (conventions 6).
rem So resolve Git-for-Windows' own bash.exe next to the git.exe on PATH.
rem
rem Keep this file pure ASCII: cmd.exe misparses a UTF-8 batch file under the
rem usual OEM codepage and swallows characters mid-line (a section sign here
rem produced `'em' is not recognized as an internal or external command`).
setlocal
for /f "delims=" %%g in ('where git.exe 2^>nul') do if not defined GIT_EXE set "GIT_EXE=%%g"
if not defined GIT_EXE echo task_worktree.cmd: git.exe not found on PATH & exit /b 2
for %%d in ("%GIT_EXE%") do set "GIT_BASH=%%~dpd..\bin\bash.exe"
if not exist "%GIT_BASH%" echo task_worktree.cmd: no Git-Bash at "%GIT_BASH%" & exit /b 2
rem Forward slashes: the .sh derives its own location from $0, and dirname on a
rem backslash path returns ".".
set "HERE=%~dp0"
set "HERE=%HERE:\=/%"
"%GIT_BASH%" "%HERE%task_worktree.sh" %*
exit /b %ERRORLEVEL%
