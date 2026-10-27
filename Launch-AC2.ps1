[CmdletBinding()]
param(
    [ValidateSet('Play', 'Editor', 'Build', 'Inspect', 'Test')]
    [string]$Mode = 'Play',
    [string]$DatDirectory = '',
    [string]$Block = '5667',
    [ValidateRange(1, 6)][int]$Radius = 2,
    [ValidateRange(0, 24)][double]$Hour = 12,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [switch]$SkipBuild,
    [switch]$Dx11,
    [switch]$Smoke,
    [switch]$UISmoke,
    [switch]$MapSmoke,
    [switch]$ControllerSmoke,
    [switch]$LoginSmoke,
    [switch]$Online,
    [ValidatePattern('^[a-zA-Z0-9.-]+$')][string]$Server='127.0.0.1',
    [ValidateRange(1,65534)][int]$Port=9000,
    [ValidatePattern('^[a-zA-Z0-9_]{0,32}$')][string]$Account='',
    [string]$AccountUrl='',
    [System.Management.Automation.PSCredential]$Credential,
    [switch]$NetworkSmoke
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 does not populate PSScriptRoot while binding defaults.
if ([string]::IsNullOrWhiteSpace($DatDirectory)) {
    $DatDirectory = Join-Path $PSScriptRoot "Asheron's Call 2 - Legions"
}
$project = Join-Path $PSScriptRoot 'Unreal/AC2Unreal.uproject'
$build = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$commandlet = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $build)) { throw "Unreal Engine 5.8 not found: $EngineRoot" }
if (!$SkipBuild) {
    & $build AC2UnrealEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) { throw "AC2 editor build failed ($LASTEXITCODE)." }
}
if ($Mode -eq 'Build') {
    & $build AC2Unreal Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) { throw "AC2 game build failed ($LASTEXITCODE)." }
    return
}
foreach ($file in @('portal.dat', 'cell_1.dat', 'local_English.dat')) {
    if (!(Test-Path -LiteralPath (Join-Path $DatDirectory $file))) { throw "Missing $file in $DatDirectory" }
}
if ($Mode -eq 'Inspect') {
    & $commandlet $project -run=AC2Inspect "-AC2DatDir=$DatDirectory" "-AC2Block=$Block" -FullTerrain -unattended -nop4 -NullRHI -stdout -FullStdOutLogOutput
    if ($LASTEXITCODE -ne 0) { throw "AC2 DAT inspection failed ($LASTEXITCODE)." }
    return
}
$materialRoot = Join-Path $PSScriptRoot 'Unreal/Content/AC2/Materials'
$missingMaterials = @('M_AC2Opaque','M_AC2Cutout','M_AC2Sky','M_AC2SkyAlpha','M_AC2SkyAdd','M_AC2Water','M_AC2Far','M_AC2Particle','M_AC2SceneryAlpha','M_AC2SceneryAdd','M_AC2ParticleAdd','M_AC2Grass') | Where-Object { !(Test-Path -LiteralPath (Join-Path $materialRoot ($_ + '.uasset'))) }
$materialVersion = Join-Path $materialRoot 'template-version.txt'
$needsMaterialUpdate = !(Test-Path -LiteralPath $materialVersion)
if (!$needsMaterialUpdate) { $needsMaterialUpdate = (Get-Content -LiteralPath $materialVersion -Raw).Trim() -ne '4' }
if ($missingMaterials -or $needsMaterialUpdate) {
    & $commandlet $project -run=AC2Inspect -MaterialsOnly -unattended -nop4 -NullRHI -stdout -FullStdOutLogOutput
    if ($LASTEXITCODE -ne 0) { throw "AC2 material preparation failed ($LASTEXITCODE)." }
}
if ($Mode -eq 'Test') {
    $report = Join-Path $PSScriptRoot ('Unreal/Saved/Automation/Run-' + [Guid]::NewGuid().ToString('N'))
    & $commandlet $project "-AC2DatDir=$DatDirectory" -unattended -nop4 -nosplash -NullRHI '-ExecCmds=Automation RunTests AC2.Data' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$report" -stdout -FullStdOutLogOutput
    if ($LASTEXITCODE -ne 0) { throw "AC2 tests failed ($LASTEXITCODE)." }
    $result = Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw | ConvertFrom-Json
    if ($result.failed -ne 0 -or $result.succeeded -lt 30) { throw 'AC2 automation did not complete all expected tests.' }
    Write-Output "AC2 tests passed. Report: $report"
    return
}
$hourText = $Hour.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$loginTicket=''
if($Online -or $NetworkSmoke -or $LoginSmoke -or $AccountUrl -or $PSBoundParameters.ContainsKey('Server')) {
    $Online=$true
    $bridge=Join-Path $PSScriptRoot 'Network/Bridge/target/release/ac2-network-bridge.exe'
    if(!(Test-Path -LiteralPath $bridge)){ & (Join-Path $PSScriptRoot 'Build-AC2Network.ps1') }
    if($AccountUrl){
        $uri=[Uri]$AccountUrl
        if(!$uri.IsAbsoluteUri -or ($uri.Scheme -ne 'https' -and !($uri.Scheme -eq 'http' -and $uri.IsLoopback)) -or $uri.UserInfo){throw 'Account login requires HTTPS (HTTP is allowed only on loopback).'}
        if($Credential){
        $secret=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($Credential.Password)
        try {
            $body=@{account=$Credential.UserName;password=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($secret)} | ConvertTo-Json -Compress
            $login=Invoke-RestMethod -Method Post -Uri ($AccountUrl.TrimEnd('/')+'/api/login') -ContentType 'application/json' -Body $body -TimeoutSec 20
        } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secret);$body=$null;$Credential=$null }
        if($login.ticket -notmatch '^[0-9a-f]{64}$' -or $login.account -notmatch '^[a-z0-9_]{1,32}$' -or $login.game_host -notmatch '^[a-zA-Z0-9.-]+$' -or $login.game_port -lt 1 -or $login.game_port -gt 65534){throw 'Invalid account-service response.'}
        $loginTicket=$login.ticket;$Account=$login.account;$Server=$login.game_host;$Port=$login.game_port;$login=$null
        }
    }
}
$launchArgs = @(('"' + $project + '"'), ('-AC2DatDir="' + $DatDirectory + '"'), "-AC2Block=$Block", "-AC2Radius=$Radius", "-AC2Hour=$hourText", '-nosplash')
if($Online){$launchArgs+=@('-AC2Online',"-AC2Server=$Server","-AC2Port=$Port");if($Account){$launchArgs+="-AC2Account=$Account"}}
if($AccountUrl){$launchArgs+=('-AC2AccountUrl="'+$AccountUrl+'"')}
if ($Mode -eq 'Play') { $launchArgs += @('-game', '-windowed', '-ResX=1600', '-ResY=900') }
if ($Dx11) { $launchArgs += '-d3d11' }
if ($Smoke -and $UISmoke) { throw 'Choose -Smoke or -UISmoke for a single verification run.' }
if ($MapSmoke -and ($Smoke -or $UISmoke -or $NetworkSmoke -or $Online)) { throw '-MapSmoke requires a separate offline verification run.' }
if ($MapSmoke) {
    $smokeReport = Join-Path $PSScriptRoot 'Unreal/Saved/Diagnostics/AC2Map-smoke.txt'
    if (Test-Path -LiteralPath $smokeReport) { Remove-Item -LiteralPath $smokeReport }
    $launchArgs += @('-AC2MapSmoke', '-RenderOffscreen', '-unattended', '-ForceRes', '-nosound', ('-abslog="' + (Join-Path $PSScriptRoot 'Unreal/Saved/Logs/AC2Map-smoke.log') + '"'))
}
if ($UISmoke) {
    $smokeReport = Join-Path $PSScriptRoot 'Unreal/Saved/Diagnostics/AC2UI-smoke.txt'
    if (Test-Path -LiteralPath $smokeReport) { Remove-Item -LiteralPath $smokeReport }
    $launchArgs += @('-AC2UISmoke', '-RenderOffscreen', '-unattended', '-ForceRes', '-nosound', ('-abslog="' + (Join-Path $PSScriptRoot 'Unreal/Saved/Logs/AC2UI-smoke.log') + '"'))
}
if ($Smoke) {
    $smokeReport = Join-Path $PSScriptRoot ('Unreal/Saved/Diagnostics/Smoke-' + [Guid]::NewGuid().ToString('N') + '.txt')
    $launchArgs += @('-AC2Smoke', ('-AC2SmokeReport="' + $smokeReport + '"'), '-RenderOffscreen', '-unattended', '-ForceRes', '-nosound')
}
# Interactive launch requested by the caller; compiler and commandlet modes stay in this terminal.
if($NetworkSmoke -or $LoginSmoke){
    $launchArgs+='-LogCmds="LogTemp Verbose"'
    $smokeReport=Join-Path $PSScriptRoot 'Unreal/Saved/Diagnostics/AC2Network-smoke.txt'
    if(Test-Path -LiteralPath $smokeReport){Remove-Item -LiteralPath $smokeReport}
    $smokeFlag=if($LoginSmoke){'-AC2LoginSmoke'}else{'-AC2NetworkSmoke'}
    $launchArgs+=@($smokeFlag,'-RenderOffscreen','-unattended','-ForceRes','-nosound',('-abslog="'+(Join-Path $PSScriptRoot 'Unreal/Saved/Logs/AC2Network-smoke.log')+'"'))
}
if($ControllerSmoke){
    if($Smoke -or $UISmoke -or $MapSmoke -or $NetworkSmoke -or $Online){throw '-ControllerSmoke requires a separate offline run.'}
    $smokeReport=Join-Path $PSScriptRoot 'Unreal/Saved/Diagnostics/AC2Controller-smoke.txt'
    if(Test-Path -LiteralPath $smokeReport){Remove-Item -LiteralPath $smokeReport}
    $launchArgs+=@('-AC2ControllerSmoke','-RenderOffscreen','-unattended','-ForceRes','-nosound',('-abslog="'+(Join-Path $PSScriptRoot 'Unreal/Saved/Logs/AC2Controller-smoke.log')+'"'))
}
if ($Smoke -or $UISmoke -or $NetworkSmoke -or $LoginSmoke -or $MapSmoke -or $ControllerSmoke) {
    $smokeStart = New-Object System.Diagnostics.ProcessStartInfo
    $smokeStart.FileName = $editor
    $smokeStart.Arguments = $launchArgs -join ' '
    $smokeStart.WorkingDirectory = $PSScriptRoot
    $smokeStart.UseShellExecute = $false
    $smokeStart.CreateNoWindow = $true
    $smokeStart.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    if($loginTicket){$smokeStart.EnvironmentVariables['AC2_LOGIN_TICKET']=$loginTicket}
    $process = [System.Diagnostics.Process]::Start($smokeStart)
    if (!$process.WaitForExit(180000)) { $process.Kill(); throw 'AC2 render smoke test exceeded three minutes.' }
    $smokeExit = $process.ExitCode
    $process.Dispose()
    if ($smokeExit -ne 0) { throw "AC2 render smoke test failed ($smokeExit). See Unreal/Saved/Logs." }
    if (!(Test-Path -LiteralPath $smokeReport) -or (Get-Content -LiteralPath $smokeReport -Raw).Trim() -ne 'PASS') {
        throw 'AC2 exited without completing the render smoke test. See Unreal/Saved/Logs.'
    }
    if($ControllerSmoke){Write-Output 'AC2 frontend/controller smoke passed. Screenshots: Unreal/Saved/Screenshots/AC2Controller-*.'}
    elseif($NetworkSmoke -or $LoginSmoke){Write-Output 'AC2 network render smoke passed: server character, world objects, chat, combat, loot and native inventory/vitals.'}
    elseif ($MapSmoke) { Write-Output 'AC2 map smoke passed: DAT world/detail/continent maps, pan, player marker, radar targeting, coordinate toggle, interior handling and input restoration.' }
    elseif ($UISmoke) {
        Write-Output 'AC2 UI smoke passed: ten native avatars, stable appearance camera, DAT fonts, eye FX, vitals, local chat, inventory, stats, persistence and input restoration.'
    } else {
        Write-Output 'AC2 world smoke passed: native HUD, streamed scenery FX, multiple emitters, rain/shelter, player, offline NPCs, water, interiors, LOD, streaming and sky.'
    }
} else {
    $playStart=New-Object System.Diagnostics.ProcessStartInfo
    $playStart.FileName=$editor;$playStart.Arguments=$launchArgs -join ' ';$playStart.WorkingDirectory=$PSScriptRoot;$playStart.UseShellExecute=$false
    if($loginTicket){$playStart.EnvironmentVariables['AC2_LOGIN_TICKET']=$loginTicket}
    $process=[System.Diagnostics.Process]::Start($playStart)
    $loginTicket=$null;$playStart.EnvironmentVariables.Remove('AC2_LOGIN_TICKET')
    if ($process.WaitForExit(3000)) { throw "AC2 closed during startup ($($process.ExitCode)). See Unreal/Saved/Logs." }
    Write-Output "AC2 launched (process $($process.Id))."
}

