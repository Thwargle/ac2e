[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$Inno = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    [string]$Python = 'python',
    [switch]$UseArchivedGame
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$project = Join-Path $root 'Unreal/AC2Unreal.uproject'
$line = Select-String -Path (Join-Path $root 'Unreal/Config/DefaultGame.ini') -Pattern '^ProjectVersion=(\d+\.\d+\.\d+)$'
if (@($line).Count -ne 1) { throw 'Set one three-part ProjectVersion in DefaultGame.ini.' }
$version = $line.Matches[0].Groups[1].Value
$out = Join-Path $root "Saved/PublicRelease/$version"
$archive = Join-Path $out 'game'
$payload = Join-Path $archive 'Windows'
$native = Join-Path $payload 'AC2Unreal/Binaries/Win64'
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)." }
}
if (!$UseArchivedGame) {
    # Build.bat uses a machine-shared lock under TMP. Keep this release independent of running editors.
    $env:TMP = (New-Item -ItemType Directory -Path (Join-Path $out 'cook-temp') -Force).FullName
    & (Join-Path $root 'Build-AC2Network.ps1') -BridgeOnly
    & (Join-Path $root 'Build-AC2Audio.ps1')
    Run (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') @('AC2UnrealEditor','Win64','Development',"-Project=$project",'-WaitMutex','-NoHotReloadFromIDE')
    Run (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') @($project,'-run=AC2Inspect','-MaterialsOnly','-unattended','-nop4','-NullRHI')
    Run (Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat') @('BuildCookRun',"-project=$project",'-noP4','-platform=Win64','-clientconfig=Shipping','-build','-cook','-stage','-pak','-archive',"-archivedirectory=$archive",'-unattended','-utf8output')
}
if (!(Test-Path (Join-Path $native 'AC2Unreal-Win64-Shipping.exe'))) { throw 'The cooked Shipping game is missing.' }
Copy-Item -LiteralPath (Join-Path $root 'Network/Bridge/target/release/ac2-network-bridge.exe'),(Join-Path $root 'Audio/Bridge/target/release/ac2-audio-player.exe') -Destination $native
$notices = Join-Path $payload 'Notices'
New-Item -ItemType Directory -Path $notices -Force | Out-Null
foreach ($pair in @(@('References/AC2_Rusted/LICENSE','AGPL-3.0.txt'),@('References/AC2_Rusted/NOTICE.md','AC2_Rusted.md'),@('Network/Bridge/NOTICE.md','Network.md'),@('Audio/Bridge/NOTICE.md','Audio.md'),@('Release/Credits.txt','Credits.txt'))) {
    Copy-Item -LiteralPath (Join-Path $root $pair[0]) -Destination (Join-Path $notices $pair[1])
}
Run $Python @((Join-Path $PSScriptRoot 'bridge_notices.py'),$root,$notices)
$compiler = Join-Path $env:SystemRoot 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$metadata = Join-Path $out 'Version.cs'
Set-Content -LiteralPath $metadata -Value "[assembly:System.Reflection.AssemblyVersion(`"$version.0`")]"
Push-Location $PSScriptRoot
try {
    Run $compiler @('/nologo','/target:winexe',"/out:$payload/AC2Launcher.exe",'/reference:System.Windows.Forms.dll,System.Drawing.dll,System.Net.Http.dll,System.Web.Extensions.dll','Launcher.cs','UpdateFeed.cs',$metadata)
    $tests = Join-Path $out 'UpdateTests.exe'
    Run $compiler @('/nologo',"/out:$tests",'/reference:System.Net.Http.dll,System.Web.Extensions.dll','UpdateTests.cs','UpdateFeed.cs')
    Run $tests @()
} finally { Pop-Location }
$unexpected = Get-ChildItem -LiteralPath $payload -File -Recurse | Where-Object { $_.Extension -in '.sdat','.journal','.pcap','.pcapng','.pem','.pfx' -or $_.Name -in 'portal.dat','cell_1.dat','cell_2.dat','highres.dat','local_English.dat','profiles.json' -or $_.FullName -match '[\\/]Saved[\\/]Login[\\/]' }
if ($unexpected) { throw "Private data found in game payload: $($unexpected[0].Name)" }
Run $Inno @("/DReleaseVersion=$version","/DPayload=$payload","/DReleaseOut=$out","/DRedist=$EngineRoot/Engine/Extras/Redist/en-us",(Join-Path $PSScriptRoot 'Windows.iss'))
Run $Python @((Join-Path $PSScriptRoot 'release_site.py'),$out,$version)

