# build_fork.ps1 - B1.1 fork build pipeline (Windows host, JDK 8; excluded from WSL CI).
#
# Builds the vendored CommunicationMod-oracle fork (tools/oracle_bridge/
# communicationmod-oracle/) into a mod jar without Maven:
#   1. Stages sources with the gson package relocation the upstream pom's
#      maven-shade-plugin performs (com.google.gson -> com.autoplay.gson).
#   2. Takes the relocated gson 2.8.5 classes from the stock workshop jar
#      (item 2131373661) instead of downloading gson: byte-identical gson
#      bytecode to the jar that produced the B0.2 baseline capture.
#   3. Compiles with JDK 8 javac against desktop-1.0.jar + ModTheSpire + BaseMod.
#   4. Packages a DETERMINISTIC jar (sorted entries, fixed timestamps, no
#      manifest) and deploys it to <game>\mods\CommunicationMod-oracle.jar.
#
# The jar is a build artifact - never committed (ledger "Never commit" list).
# Usage:  powershell -File build_fork.ps1 [-NoDeploy] [-CheckDeterminism]
param(
    [string]$GameDir      = 'D:\SteamLibrary\steamapps\common\SlayTheSpire',
    [string]$WorkshopRoot = 'D:\SteamLibrary\steamapps\workshop\content\646570',
    [string]$Jdk          = 'C:\Program Files\Java\jdk1.8.0_171',
    [string]$OutDir       = '',
    [switch]$NoDeploy,
    [switch]$CheckDeterminism
)
$ErrorActionPreference = 'Stop'

$ForkDir    = Join-Path $PSScriptRoot 'communicationmod-oracle'
$RepoRoot   = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($OutDir -eq '') { $OutDir = Join-Path $RepoRoot 'build\oracle_fork' }

$Javac      = Join-Path $Jdk 'bin\javac.exe'
$DesktopJar = Join-Path $GameDir 'desktop-1.0.jar'
$MtsJar     = Join-Path $WorkshopRoot '1605060445\ModTheSpire.jar'
$BaseModJar = Join-Path $WorkshopRoot '1605833019\BaseMod.jar'
$StockJar   = Join-Path $WorkshopRoot '2131373661\CommunicationMod.jar'

foreach ($p in @($Javac, $DesktopJar, $MtsJar, $BaseModJar, $StockJar, $ForkDir)) {
    if (-not (Test-Path $p)) { throw "missing prerequisite: $p" }
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# Zip entry timestamps are the one nondeterministic input; pin them to the
# game build date. Value is arbitrary but frozen - changing it changes the hash.
$FixedStamp = [DateTimeOffset]::new(2020, 11, 30, 0, 0, 0, [TimeSpan]::Zero)

function Build-ForkJar([string]$Work) {
    if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
    $srcDir  = Join-Path $Work 'src'
    $clsDir  = Join-Path $Work 'classes'
    $gsonDir = Join-Path $Work 'gson'
    $rootDir = Join-Path $Work 'jar_root'
    foreach ($d in @($srcDir, $clsDir, $gsonDir, $rootDir)) {
        New-Item -ItemType Directory -Force $d | Out-Null
    }

    # -- 1. Stage sources, applying the pom's shade relocation textually.
    $javaRoot = Join-Path $ForkDir 'src\main\java'
    $staged = @()
    Get-ChildItem $javaRoot -Recurse -Filter *.java | ForEach-Object {
        $rel  = $_.FullName.Substring($javaRoot.Length + 1)
        $dest = Join-Path $srcDir $rel
        New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
        $text = [IO.File]::ReadAllText($_.FullName)
        [IO.File]::WriteAllText($dest, $text.Replace('com.google.gson', 'com.autoplay.gson'))
        $staged += $dest
    }
    if ($staged.Count -eq 0) { throw "no .java sources under $javaRoot" }

    # -- 2. Extract the relocated gson classes from the stock workshop jar.
    $zip = [IO.Compression.ZipFile]::OpenRead($StockJar)
    try {
        $gsonEntries = @($zip.Entries | Where-Object {
            $_.FullName.StartsWith('com/autoplay/gson/') -and $_.Name -ne ''
        })
        if ($gsonEntries.Count -lt 100) { throw "stock jar gson extraction found only $($gsonEntries.Count) entries" }
        foreach ($e in $gsonEntries) {
            $dest = Join-Path $gsonDir ($e.FullName -replace '/', '\')
            New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $true)
        }
    } finally { $zip.Dispose() }

    # -- 3. Compile (JDK 8).
    $listFile = Join-Path $Work 'sources.txt'
    $staged | Sort-Object | Set-Content -Encoding Ascii $listFile
    $cp = "$DesktopJar;$MtsJar;$BaseModJar;$gsonDir"
    # -g (full debug info) is REQUIRED: ModTheSpire resolves @SpirePatch
    # parameter names from the LocalVariableTable; without it patching dies
    # with "Illegal patch parameter: Cannot determine name". Maven's default.
    & $Javac -encoding UTF-8 -g -cp $cp -d $clsDir "@$listFile"
    if ($LASTEXITCODE -ne 0) { throw "javac failed (exit $LASTEXITCODE)" }

    # -- 4. Assemble jar root: classes + relocated gson + resources + license.
    Copy-Item -Recurse -Force (Join-Path $clsDir '*')  $rootDir
    Copy-Item -Recurse -Force (Join-Path $gsonDir 'com') $rootDir
    $resDir = Join-Path $ForkDir 'src\main\resources'
    Copy-Item (Join-Path $resDir 'ModTheSpire.json') $rootDir
    Copy-Item (Join-Path $resDir 'Icon.png')         $rootDir
    Copy-Item (Join-Path $ForkDir 'LICENSE')         $rootDir

    # -- 5. Deterministic zip: ordinal-sorted entries, pinned timestamps.
    $jarPath = Join-Path $Work 'CommunicationMod-oracle.jar'
    $files = Get-ChildItem $rootDir -Recurse -File | ForEach-Object {
        $_.FullName.Substring($rootDir.Length + 1) -replace '\\', '/'
    } | Sort-Object { $_ } -CaseSensitive
    $out = [IO.File]::Open($jarPath, [IO.FileMode]::Create)
    try {
        $archive = New-Object IO.Compression.ZipArchive($out, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($rel in $files) {
                $entry = $archive.CreateEntry($rel, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $FixedStamp
                $es = $entry.Open()
                try {
                    $fs = [IO.File]::OpenRead((Join-Path $rootDir ($rel -replace '/', '\')))
                    try { $fs.CopyTo($es) } finally { $fs.Dispose() }
                } finally { $es.Dispose() }
            }
        } finally { $archive.Dispose() }
    } finally { $out.Dispose() }
    return $jarPath
}

$jar = Build-ForkJar $OutDir
$hash = (Get-FileHash -Algorithm SHA256 $jar).Hash
Write-Host "built:  $jar"
Write-Host "sha256: $hash"

if ($CheckDeterminism) {
    $jar2  = Build-ForkJar (Join-Path $OutDir 'determinism_check')
    $hash2 = (Get-FileHash -Algorithm SHA256 $jar2).Hash
    if ($hash2 -ne $hash) { throw "NOT deterministic: $hash vs $hash2" }
    Write-Host "determinism: PASS (second full build byte-identical)"
}

if (-not $NoDeploy) {
    $modsDir = Join-Path $GameDir 'mods'
    New-Item -ItemType Directory -Force $modsDir | Out-Null
    $deployed = Join-Path $modsDir 'CommunicationMod-oracle.jar'
    Copy-Item -Force $jar $deployed
    Write-Host "deployed: $deployed"
}
