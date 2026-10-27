[CmdletBinding()]
param(
    [string]$Bind='127.0.0.1:19400',
    [string]$PublicOrigin='http://127.0.0.1:19400',
    [string]$GameHost='127.0.0.1',
    [ValidateRange(1024,65534)][int]$GamePort=9000,
    [string]$AuthDirectory='',
    [string]$DataDirectory='',
    [switch]$Stop
)
$ErrorActionPreference='Stop'
if(!$AuthDirectory){$AuthDirectory=Join-Path $PSScriptRoot 'data/accounts'}
if(!$DataDirectory){$DataDirectory=Join-Path $PSScriptRoot 'data/world'}
$server=Join-Path $PSScriptRoot 'target/release/ac2e.exe'
if($Stop){& $server account-stop --auth-data $AuthDirectory --seconds 20;return}
if(!(Test-Path -LiteralPath $server)){throw 'Build AC2E first with AC2E/Build-AC2E.ps1.'}
$env:AC2E_PUBLIC_ORIGIN=$PublicOrigin
& $server account-web --auth-data $AuthDirectory --data $DataDirectory --web-bind $Bind --public-origin $PublicOrigin --public-host $GameHost --public-port $GamePort
if($LASTEXITCODE){throw "Account service exited with code $LASTEXITCODE"}
