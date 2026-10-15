. "$PSScriptRoot\mtp.ps1"

# make sure a folder path exists under SD Card, creating each missing part
function mtp-ensure([string]$path) {
    $f = mtp-root
    foreach ($part in ($path -split '/' | Where-Object { $_ })) {
        $it = $f.Items() | Where-Object { $_.Name -eq $part }
        if (-not $it) {
            $f.NewFolder($part)
            for ($i = 0; $i -lt 40 -and -not $it; $i++) {
                Start-Sleep -Milliseconds 250
                $it = $f.Items() | Where-Object { $_.Name -eq $part }
            }
            if (-not $it) { throw "could not create $part in $path" }
        }
        $f = $it.GetFolder
    }
    $f
}

# copy every file under $local into the same relative place on the SD card
function mtp-put-tree([string]$local) {
    $root = (Resolve-Path $local).Path
    Get-ChildItem $root -Recurse -Directory | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
        mtp-ensure $rel | Out-Null
    }
    Get-ChildItem $root -Recurse -File | ForEach-Object {
        $rel = $_.DirectoryName.Substring($root.Length).TrimStart('\').Replace('\', '/')
        $dest = mtp-ensure $rel
        $name = $_.Name
        $dest.CopyHere($_.FullName, 1044)
        $seen = $null
        for ($i = 0; $i -lt 120 -and -not $seen; $i++) {
            Start-Sleep -Milliseconds 250
            $seen = $dest.Items() | Where-Object { $_.Name -eq $name }
        }
        if ($seen) { "copied  $rel/$name" } else { "MISSING $rel/$name" }
    }
}
