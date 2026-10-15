# MTP helpers for the Switch (DBI). Usage:
#   . mtp.ps1; mtp-ls "emuMMC"            list a folder under SD Card
#   mtp-get "emuMMC/RAW1/Nintendo/Album/2026/10/03" "C:\dest"   copy every file in it
#   mtp-put "switch" "C:\file"           copy a file into a folder (overwrites)
$global:sh = New-Object -ComObject Shell.Application

function mtp-root {
    $dev = $global:sh.NameSpace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch' }
    if (-not $dev) { throw 'Switch not connected' }
    ($dev.GetFolder.Items() | Where-Object { $_.Name -eq 'SD Card' }).GetFolder
}

function mtp-folder([string]$path) {
    $f = mtp-root
    foreach ($part in ($path -split '/' | Where-Object { $_ })) {
        $it = $f.Items() | Where-Object { $_.Name -eq $part }
        if (-not $it) { throw "not found: $part in $path" }
        $f = $it.GetFolder
    }
    $f
}

function mtp-ls([string]$path) {
    (mtp-folder $path).Items() | ForEach-Object { '{0}{1}' -f $_.Name, ($(if ($_.IsFolder) { '/' } else { '' })) }
}

function mtp-get([string]$path, [string]$dest, [string]$like = '*') {
    New-Item -ItemType Directory -Force $dest | Out-Null
    $d = $global:sh.NameSpace($dest)
    $items = (mtp-folder $path).Items() | Where-Object { -not $_.IsFolder -and $_.Name -like $like }
    foreach ($it in $items) {
        $d.CopyHere($it, 1044)
        $target = Join-Path $dest $it.Name
        for ($i = 0; $i -lt 300 -and -not (Test-Path $target); $i++) { Start-Sleep -Milliseconds 500 }
    }
    Start-Sleep -Seconds 3
    $items | ForEach-Object { $_.Name }
}

function mtp-put([string]$path, [string]$file) {
    (mtp-folder $path).CopyHere($file, 1044)
}

function mtp-rename([string]$path, [string]$name, [string]$new) {
    $it = (mtp-folder $path).Items() | Where-Object { $_.Name -eq $name }
    if (-not $it) { throw "not found: $name in $path" }
    $it.Name = $new
}

function mtp-rm([string]$path, [string]$name) {
    $it = (mtp-folder $path).Items() | Where-Object { $_.Name -eq $name }
    if (-not $it) { return }
    $trash = Join-Path $env:TEMP ("mtp_rm_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force $trash | Out-Null
    $global:sh.NameSpace($trash).MoveHere($it, 1044)
    for ($i = 0; $i -lt 120 -and ((mtp-ls $path) -contains $name); $i++) { [System.Threading.Thread]::Sleep(500) }
    Remove-Item -Recurse -Force $trash -ErrorAction SilentlyContinue
    if ((mtp-ls $path) -contains $name) { throw "couldn't delete $name in $path" }
}
