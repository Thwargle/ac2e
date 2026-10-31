[CmdletBinding()]
param([switch]$Test)
$ErrorActionPreference='Stop'
$cargo=Join-Path $PSScriptRoot 'Tools/Rust/cargo/bin/cargo.exe'
if(Test-Path -LiteralPath $cargo) {
    $env:CARGO_HOME=Join-Path $PSScriptRoot 'Tools/Rust/cargo'
    $env:RUSTUP_HOME=Join-Path $PSScriptRoot 'Tools/Rust/rustup'
    $env:PATH=(Split-Path $cargo)+';'+$env:PATH
} else { $cargo='cargo' }
if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'References/AC2_Rusted/crates/ac2-audio/Cargo.toml'))) {
    throw 'Audio requires the pinned reference: run git submodule update --init --recursive.'
}
$manifest=Join-Path $PSScriptRoot 'Audio/Bridge/Cargo.toml'
$referenceHead=& git -C (Join-Path $PSScriptRoot 'References/AC2_Rusted') rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $referenceHead -ne 'bcf67b4b9ac3ee9eb429d68ba6766cfce47c5a25'){throw 'AC2_Rusted reference does not match the supported audio revision.'}
& $cargo build --release --locked --manifest-path $manifest
if($LASTEXITCODE -ne 0){throw 'AC2 DAT audio build failed.'}
if($Test){
    & $cargo test --locked --manifest-path $manifest -- --nocapture
    if($LASTEXITCODE -ne 0){throw 'AC2 DAT audio tests failed.'}
}
