[CmdletBinding()]
param([switch]$Test,[string]$Python='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$localCargo=Join-Path $root 'Tools/Rust/cargo/bin/cargo.exe'
$cargo='cargo'
if(Test-Path -LiteralPath $localCargo){
    $cargo=$localCargo
    $env:CARGO_HOME=Join-Path $root 'Tools/Rust/cargo'
    $env:RUSTUP_HOME=Join-Path $root 'Tools/Rust/rustup'
    $env:PATH=(Split-Path $cargo)+';'+$env:PATH
} elseif(!(Get-Command cargo -ErrorAction SilentlyContinue)) {
    throw 'Rust toolchain missing. Install Rust with rustup and reopen your terminal so cargo is on PATH. An already-built server can be started with start-ac2e.bat without Rust.'
}
if(!$Python){$Python=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';if(!(Test-Path -LiteralPath $Python)){$Python='python'}}
& $Python (Join-Path $PSScriptRoot 'export_source.py')
if($LASTEXITCODE){throw 'Source archive generation failed.'}
& $cargo build --release --locked --manifest-path (Join-Path $PSScriptRoot 'emulator/Cargo.toml') --target-dir (Join-Path $PSScriptRoot 'target')
if($LASTEXITCODE){throw 'AC2E build failed.'}
if($Test){
    & $cargo test --locked --manifest-path (Join-Path $PSScriptRoot 'emulator/Cargo.toml') --target-dir (Join-Path $PSScriptRoot 'target')
    if($LASTEXITCODE){throw 'AC2E tests failed.'}
}
