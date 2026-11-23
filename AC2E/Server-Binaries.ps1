# Game builds must not overwrite the executable held by a standalone account
# service. Keep the legacy path for accounts and for existing installations.
function Get-AC2EGameBinary {
    $game=Join-Path $PSScriptRoot 'target/game/release/ac2e.exe'
    # A running game holds its executable on Windows. A tested release may be
    # staged beside it and picked up on the next start. A newer ordinary build
    # takes precedence without disturbing the running process.
    $staged=Join-Path $PSScriptRoot 'target/game-next/release/ac2e.exe'
    if((Test-Path -LiteralPath $staged -PathType Leaf) -and
       (!(Test-Path -LiteralPath $game -PathType Leaf) -or
        [IO.File]::GetLastWriteTimeUtc($staged) -gt [IO.File]::GetLastWriteTimeUtc($game))){return $staged}
    if(Test-Path -LiteralPath $game -PathType Leaf){return $game}
    return (Join-Path $PSScriptRoot 'target/release/ac2e.exe')
}
function Get-AC2EManagedBinaries {
    return @((Join-Path $PSScriptRoot 'target/game/release/ac2e.exe'),
             (Join-Path $PSScriptRoot 'target/game-next/release/ac2e.exe'),
             (Join-Path $PSScriptRoot 'target/release/ac2e.exe'))
}
