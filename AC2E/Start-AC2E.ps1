[CmdletBinding()]
param(
    [string]$Bind='127.0.0.1',
    [ValidateRange(1,65534)][int]$Port=9000,
    [string]$Account='local',
    [string]$DatDirectory='',
    [string]$DataDirectory='',
    [string]$AuthDirectory='',
    [string]$PublicOrigin='',
    [string]$Content='',
    [string]$Python='',
    [switch]$SkipBuild,
    [switch]$Rebuild,
    [switch]$Stop
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if(!$Python){$Python=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';if(!(Test-Path -LiteralPath $Python)){$Python='python'}}
if(!$DatDirectory){$DatDirectory=Join-Path $root "Asheron's Call 2 - Legions"}
if(!$DataDirectory){$DataDirectory=Join-Path $PSScriptRoot 'data/world'}
$server=Join-Path $PSScriptRoot 'target/release/ac2e.exe'
if($Stop){& $server stop --data $DataDirectory --seconds 20; if($LASTEXITCODE){throw 'Server stop failed.'};return}
if($SkipBuild -and $Rebuild){throw 'Choose either -SkipBuild or -Rebuild.'}
if($Rebuild -or (!(Test-Path -LiteralPath $server -PathType Leaf) -and !$SkipBuild)){
    & (Join-Path $PSScriptRoot 'Build-AC2E.ps1') -Python $Python
}
if(!(Test-Path -LiteralPath $server -PathType Leaf)){
    throw 'AC2E server binary is missing. Run AC2E/Build-AC2E.ps1, or start without -SkipBuild to build it.'
}
if(!$Content){
    $Content=Join-Path $DataDirectory 'legions-world.tsv'
    & $Python (Join-Path $root 'Network/prepare_content.py') $DatDirectory $Content
    if($LASTEXITCODE){throw 'DAT content preparation failed.'}
}
$ip=[Net.IPAddress]::Parse($Bind)
if(![Net.IPAddress]::IsLoopback($ip) -and !$AuthDirectory){throw 'Non-loopback hosting requires an initialized account service (-AuthDirectory).'}
if(![Net.IPAddress]::IsLoopback($ip) -and $PublicOrigin -notmatch '^https://[a-zA-Z0-9.:-]+$'){throw 'Public hosting requires -PublicOrigin https://accounts.example.com, serving the account backend and its source archive.'}
$env:AC2E_PUBLIC_ORIGIN=$PublicOrigin
$env:PYREALIUM_UNREAL_LEGIONS_DATA='1'
$serverArgs=@('serve','--bind',"${Bind}:$Port",'--account',$Account,'--data',$DataDirectory,'--game-data',$DatDirectory,
    '--content',$Content,'--initialize-ui','true','--training-kit','false','--lifestones','true','--portals','false')
if($AuthDirectory){$serverArgs+=@('--auth-data',$AuthDirectory)}
Write-Output "AC2E: UDP $Port and $($Port+1); persistent store $DataDirectory"
& $server @serverArgs
if($LASTEXITCODE){throw "AC2E exited with code $LASTEXITCODE"}
