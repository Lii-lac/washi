# pwsh -STA -File tools\mtp_upload.ps1 <folder on the SD> <local file>
# Uploads a file to the Switch over MTP (haze or DBI):
# - it is copied under <name>.upload: Windows has no property handler for that, while for images and text files it wants to ask
#   "copy without its properties?", and over MTP such a copy then silently never happens;
# - it waits until that copy is as big as the local file (the copy is asynchronous and dies with this process), gives up after
#   90 s plus 1 s per 200 KB (each copy over haze takes about 30 s whatever its size; when nothing lands at all, haze needs
#   restarting);
# - the old copy steps aside as <name>.old, the new one takes the name, one copy back is compared by hash, and the old copy is
#   deleted. If anything fails, the old copy keeps (or gets back) its name.
param([string]$folder, [string]$src)
. (Join-Path $PSScriptRoot "mtp.ps1")
if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { "no such file: $src"; exit 1 }
$name = Split-Path $src -Leaf
$size = (Get-Item -LiteralPath $src).Length
$want = (Get-FileHash -LiteralPath $src).Hash
$up = "$name.upload"

function names { try { @(mtp-ls $folder) } catch { @() } }
function remote-size([string]$nm) {
    try {
        $it = (mtp-folder $folder).Items() | Where-Object { $_.Name -eq $nm }
        if ($it) { [int64]$it.ExtendedProperty("System.Size") } else { -1 }
    } catch { -1 }
}

$n = names
if (-not $n.Count) { "the Switch's SD card isn't showing (restart haze)"; exit 1 }
foreach ($stale in "$name.old", $up) { if ($n -contains $stale) { mtp-rm $folder $stale } } # (left by a run that didn't finish)

$tmp = Join-Path $env:TEMP ("mtp_up_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $tmp | Out-Null
Copy-Item -LiteralPath $src (Join-Path $tmp $up)
mtp-put $folder (Join-Path $tmp $up)
$t0 = Get-Date
$limit = 90 + $size / 200000
$ok = $false
while (((Get-Date) - $t0).TotalSeconds -lt $limit) {
    [System.Threading.Thread]::Sleep(1000)
    if ((remote-size $up) -eq $size) { $ok = $true; break }
}
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if (-not $ok) {
    "$name didn't land within $([int]$limit) s: haze is ignoring new files (restart it)"
    exit 1
}
[System.Threading.Thread]::Sleep(1000) # (the last bytes settle)
if ((names) -contains $name) { mtp-rename $folder $name "$name.old" }
mtp-rename $folder $up $name
$d = Join-Path $env:TEMP "mtp_up_check"
Remove-Item (Join-Path $d $name) -ErrorAction SilentlyContinue
try { mtp-get $folder $d $name | Out-Null } catch {}
$got = (Get-FileHash -LiteralPath (Join-Path $d $name) -ErrorAction SilentlyContinue).Hash
if ($got -eq $want) {
    try { mtp-rm $folder "$name.old" } catch { "(couldn't delete $name.old)" } # no old versions are kept
    "$name uploaded and checked in $([int]((Get-Date) - $t0).TotalSeconds) s"
    exit 0
}
"${name}: the copy on the Switch doesn't match; the old one is put back"
try {
    mtp-rm $folder $name
    if ((names) -contains "$name.old") { mtp-rename $folder "$name.old" $name }
} catch { "couldn't put the old $name back: rename $name.old to $name on the SD card" }
exit 1
