# Windows configure + build, including safe toolchain setup for reconfiguration.
# No tests run. STS_VCVARS64 and STS_LLVM_BIN override the local defaults.
$ErrorActionPreference = 'Stop'

function Show-Usage {
    Write-Host 'Usage: tools\win_build.cmd <win-debug|win-asan|win-release> [CMake build arguments]'
    Write-Host 'Example: tools\win_build.cmd win-debug --target replay_run_diff --parallel 4'
    Write-Host 'Always configures then builds; never runs ctest or removes a build directory.'
    Write-Host 'Only one helper may run per worktree; a concurrent invocation fails immediately.'
    Write-Host 'Optional environment overrides: STS_VCVARS64 (batch file), STS_LLVM_BIN (directory).'
}

if ($args.Count -eq 0) { Show-Usage; exit 2 }
if ($args[0] -in @('--help', '-h', '/?')) { Show-Usage; exit 0 }
$preset = $args[0]
if ($preset -notin @('win-debug', 'win-asan', 'win-release')) {
    Write-Host "win_build: unsupported preset '$preset'."
    Show-Usage
    exit 2
}
$buildArgs = @($args | Select-Object -Skip 1)
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $repoRoot "build/$preset"
$cachePath = Join-Path $buildDir 'CMakeCache.txt'
$buildLock = $null

try {
    if (@($buildArgs | Where-Object { $_ -match '^--(?:preset|build)(?:=|$)' }).Count) {
        throw 'Build arguments cannot override the selected preset or build directory.'
    }
    Write-Host "win_build: $preset in $repoRoot (configure + build only)"
    if (Test-Path -LiteralPath $cachePath) {
        $flags = @(Get-Content -LiteralPath $cachePath | Where-Object { $_ -match '^CMAKE_CXX_FLAGS:[^=]+=' })
        if ($flags.Count -ne 1 -or $flags[0] -notmatch '=(?:.*\s)?/EHsc(?:\s|$)') {
            throw "Refusing existing cache without /EHsc: $cachePath`nMove aside exactly '$buildDir' to an unused backup path, then rerun this helper for a fresh configure. Existing files have not been changed."
        }
    }

    $vcvars = $env:STS_VCVARS64
    if (-not $vcvars) {
        $vcvars = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvars64.bat'
        if (-not (Test-Path -LiteralPath $vcvars)) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            if (Test-Path -LiteralPath $vswhere) {
                $vcvars = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC/Auxiliary/Build/vcvars64.bat | Select-Object -First 1
            }
        }
    }
    if (-not $vcvars -or -not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
        throw 'vcvars64.bat not found. Set STS_VCVARS64 to its full path.'
    }
    $llvmBin = $env:STS_LLVM_BIN
    if (-not $llvmBin) { $llvmBin = Join-Path $env:ProgramFiles 'LLVM/bin' }
    if (-not (Test-Path -LiteralPath (Join-Path $llvmBin 'clang-cl.exe') -PathType Leaf)) {
        throw 'clang-cl.exe not found. Set STS_LLVM_BIN to the LLVM bin directory.'
    }
    $lockPath = Join-Path $repoRoot 'build/.win_build.lock'
    [IO.Directory]::CreateDirectory((Split-Path $lockPath)) | Out-Null
    try {
        $buildLock = [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None')
    } catch [IO.IOException] {
        if (($_.Exception.HResult -band 0xffff) -notin @(32, 33)) { throw }
        throw "Another win_build helper is active for '$repoRoot' (lock: $lockPath). Wait for it to finish."
    }
    Write-Host "win_build: vcvars64=$vcvars; LLVM=$llvmBin"
    # Capture environment internally; never print an environment dump.
    $vcEnvironment = & $env:ComSpec /d /s /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvars64 failed (exit $LASTEXITCODE)." }
    foreach ($line in $vcEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $env:PATH = "$llvmBin;$env:PATH"
    Push-Location -LiteralPath $repoRoot
    try {
        & cmake --preset $preset
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & cmake --build --preset $preset @buildArgs
        exit $LASTEXITCODE
    } finally { Pop-Location }
} catch {
    [Console]::Error.WriteLine("win_build: " + $_.Exception.Message)
    exit 2
} finally {
    if ($null -ne $buildLock) { $buildLock.Dispose() }
}
