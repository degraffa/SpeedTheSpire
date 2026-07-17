<#
.SYNOPSIS
  Recompiles and reruns the JVM golden-capture harness (tools/golden_capture),
  producing the tier-1 RNG golden vectors under tests/golden/.

.DESCRIPTION
  Windows-host-only (task A0.1, docs/stage-a-tasks.md). Runs javac/java from
  PATH against D:\STS_BG_Mod\sts-classes.jar. A second jar (the real desktop
  game jar, defaulted below) is also placed on the classpath purely to supply
  org.apache.logging.log4j.* classes: com.megacrit.cardcrawl.random.Random has
  a static Logger field, so its <clinit> throws NoClassDefFoundError unless
  log4j classes are resolvable -- sts-classes.jar does not bundle them.
  Nothing is extracted from either jar; both are only ever used as -cp entries
  for a JVM we immediately throw away.

  Deterministic: every golden file is a pure function of GoldenCapture.java's
  source (fixed seed battery, fixed call sequences) plus the two jars' bytecode,
  so re-running this script must reproduce byte-identical output. The output
  directory is wiped first so stale files from a prior version of the harness
  never linger.

.PARAMETER StsClassesJar
  Classpath entry with the game classes under test (RandomXS128, Random,
  SeedHelper).

.PARAMETER DesktopJar
  Classpath entry supplying log4j (and everything else) transitively, from a
  real Slay the Spire install. Only its already-bundled log4j classes are
  actually touched by this harness.

.EXAMPLE
  pwsh tools/golden_capture/regen.ps1
#>
param(
    [string]$StsClassesJar = "D:\STS_BG_Mod\sts-classes.jar",
    [string]$DesktopJar = "D:\SteamLibrary\steamapps\common\SlayTheSpire\desktop-1.0.jar"
)

$ErrorActionPreference = "Stop"

$toolDir = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $toolDir)
$srcFile = Join-Path $toolDir "src\GoldenCapture.java"
$buildDir = Join-Path $toolDir "build"
$outDir = Join-Path $repoRoot "tests\golden"

if (-not (Test-Path $StsClassesJar)) {
    throw "sts-classes.jar not found at '$StsClassesJar' (pass -StsClassesJar to override)"
}
if (-not (Test-Path $DesktopJar)) {
    throw "desktop-1.0.jar not found at '$DesktopJar' (pass -DesktopJar to override; needed only for log4j classes)"
}

Write-Output "Cleaning build dir: $buildDir"
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir -Confirm:$false }
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Write-Output "Cleaning golden output dir: $outDir"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir -Confirm:$false }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Output "Compiling $srcFile"
& javac -cp $StsClassesJar -d $buildDir $srcFile
if ($LASTEXITCODE -ne 0) { throw "javac failed with exit code $LASTEXITCODE" }

Write-Output "Running GoldenCapture -> $outDir"
$cp = "$buildDir;$StsClassesJar;$DesktopJar"
& java -cp $cp GoldenCapture $outDir
if ($LASTEXITCODE -ne 0) { throw "java GoldenCapture failed with exit code $LASTEXITCODE" }

$files = Get-ChildItem -Path $outDir -File
$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Output ("Wrote {0} files, {1:N1} KB total, under {2}" -f $files.Count, ($totalBytes / 1KB), $outDir)
