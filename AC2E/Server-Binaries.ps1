# Game builds must not overwrite the executable held by a standalone account
# service. Keep the legacy path for accounts and for existing installations.
function Get-AC2EGameBinary {
    $game=Join-Path $PSScriptRoot 'target/game/release/ac2e.exe'
    if(Test-Path -LiteralPath $game -PathType Leaf){return $game}
    return (Join-Path $PSScriptRoot 'target/release/ac2e.exe')
}
function Get-AC2EManagedBinaries {
    return @((Join-Path $PSScriptRoot 'target/game/release/ac2e.exe'),
             (Join-Path $PSScriptRoot 'target/release/ac2e.exe'))
}
