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
$DataDirectory=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DataDirectory)
$server=Join-Path $PSScriptRoot 'target/release/ac2e.exe'
if($Stop){& $server stop --data $DataDirectory --seconds 20; if($LASTEXITCODE){throw 'Server stop failed.'};return}
if($SkipBuild -and $Rebuild){throw 'Choose either -SkipBuild or -Rebuild.'}
# The store's native exclusive lock remains authoritative. Only report a live
# owner after checking its executable and data directory, since PIDs are reused.
# Never remove a lock here: that could allow two writers into the same journals.
$lockFile=Join-Path $DataDirectory 'server.lock'
if(Test-Path -LiteralPath $lockFile -PathType Leaf){
    $lockOwner=[uint32]0
    $lockText=([string](Get-Content -LiteralPath $lockFile -Raw)).Trim()
    if(![uint32]::TryParse($lockText,[ref]$lockOwner) -or $lockOwner -eq 0){
        throw "The store is locked, but its owner could not be verified: $lockFile. Wait for any starting server to finish, then retry. The lock was left intact."
    }
    $owner=Get-CimInstance Win32_Process -Filter "ProcessId=$lockOwner"
    $ownerData=$null
    if($owner -and $owner.ExecutablePath -eq $server -and $owner.CommandLine -match '^(?:"[^"]+"|\S+)\s+serve(?:-training)?\s'){
        $dataMatch=[regex]::Match($owner.CommandLine,'(?:^|\s)--data\s+(?:"(?<quoted>[^"]+)"|(?<plain>\S+))')
        if($dataMatch.Success){
            $dataArgument=if($dataMatch.Groups['quoted'].Success){$dataMatch.Groups['quoted'].Value}else{$dataMatch.Groups['plain'].Value}
            if([IO.Path]::IsPathRooted($dataArgument)){$ownerData=[IO.Path]::GetFullPath($dataArgument)}
        }
    }
    if(!$ownerData -or $ownerData.TrimEnd('\','/') -ne $DataDirectory.TrimEnd('\','/')){
        throw "A server lock exists, but its recorded owner (PID $lockOwner) cannot be confirmed for this store: $lockFile. Verify that no server is using this store before removing a stale lock. The launcher has not removed it."
    }
    if($Rebuild){throw "AC2E is already running (PID $lockOwner). Stop it with Start-AC2E.ps1 -Stop -DataDirectory `"$DataDirectory`" before rebuilding."}
    $bindMatch=[regex]::Match($owner.CommandLine,'(?:^|\s)--bind\s+(?:"(?<quoted>[^"]+)"|(?<plain>\S+))')
    $endpoint=if($bindMatch.Groups['quoted'].Success){$bindMatch.Groups['quoted'].Value}else{$bindMatch.Groups['plain'].Value}
    Write-Output "AC2E is already running (PID $lockOwner) at $endpoint. Data: $DataDirectory"
    Write-Output 'No second server was started. Existing settings remain in effect; stop the server before changing them.'
    Write-Output "To stop: powershell.exe -File `"$PSScriptRoot\Start-AC2E.ps1`" -Stop -DataDirectory `"$DataDirectory`""
    return
}
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
