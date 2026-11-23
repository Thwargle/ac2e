# Move a stale marker aside only while holding an exclusive read/write handle.
# Live or unverifiable owners are preserved. A PID reused by a process born
# after the marker was written cannot own that marker. This also serializes two launchers
# trying to recover the same marker: neither can rename the other's fresh lock.
function Restore-AC2EStaleLock([string]$Directory, [string]$Name) {
    $rootPath=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
    $path=[IO.Path]::GetFullPath((Join-Path $rootPath $Name))
    if([IO.Path]::GetDirectoryName($path) -ne $rootPath){throw 'Lock path is outside its data directory.'}
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){return}
    $handle=$null
    try {
        $handle=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Delete)
        $reader=[IO.StreamReader]::new($handle,[Text.Encoding]::UTF8,$true,1024,$true)
        try {$marker=$reader.ReadToEnd().Trim()} finally {$reader.Dispose()}
        $ownerId=[uint32]0
        if(![uint32]::TryParse($marker,[ref]$ownerId) -or $ownerId -eq 0){return}
        $owner=Get-CimInstance Win32_Process -Filter "ProcessId=$ownerId" -ErrorAction Stop
        $reason="exited process $ownerId"
        if($owner){
            # Do not infer ownership from an executable name, nor remove a lock
            # just because its PID belongs to an unfamiliar program. Require
            # positive evidence that the recorded process lifetime has ended.
            # Read the timestamp while the marker is held against other writers.
            $written=[IO.File]::GetLastWriteTimeUtc($path)
            if(!$owner.CreationDate -or $owner.CreationDate.ToUniversalTime() -le $written.AddSeconds(2)){return}
            $reason="reused PID $ownerId (current process started after the marker)"
        }
        $archive=Join-Path $rootPath ($Name+'.stale-'+[Guid]::NewGuid().ToString('N'))
        [IO.File]::Move($path,$archive)
        Write-Output "Recovered stale $Name from $reason. Preserved marker: $archive"
    } finally {if($handle){$handle.Dispose()}}
}
