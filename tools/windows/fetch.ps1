# SPDX-License-Identifier: GPL-2.0-only
# NX System On Linux -- fetch the Windows build environment
# Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
#
# Downloads full official installers into tools\windows\environment\ and then
# STOPS. It does not install anything and does not touch PATH -- you run each
# installer yourself, in the order printed at the end.
#
#   .\fetch.ps1              download everything
#   .\fetch.ps1 -List        show what would be downloaded and why
#   .\fetch.ps1 -CheckPath   report whether the tools are already on PATH
#   .\fetch.ps1 -OpenPages   open the official download pages in a browser

param(
    [switch]$List,
    [switch]$CheckPath,
    [switch]$OpenPages
)

$ErrorActionPreference = "Stop"

# Where the installers land. Sits next to this script.
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Env  = Join-Path $Root "environment"

# ---------------------------------------------------------------------------
# The tool list.
#
# Two of these have URLs that never change, so they are written out. QEMU does
# not: its Windows builds are published as qemu-w64-setup-YYYYMMDD.exe and the
# date changes whenever Stefan Weil rebuilds, so a hard-coded file name would
# rot within weeks. For that one the script reads the directory listing and
# takes the newest.
#
# OVMF is deliberately absent. The QEMU installer ships it:
#   C:\Program Files\qemu\share\edk2-x86_64-code.fd
#   C:\Program Files\qemu\share\edk2-i386-vars.fd
# so there is nothing extra to fetch.
# ---------------------------------------------------------------------------
$Tools = @(
    @{
        Name    = "LLVM"
        File    = "LLVM-19.1.0-win64.exe"
        Url     = "https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.0/LLVM-19.1.0-win64.exe"
        # Published alongside the release assets; used to reject a corrupt or
        # replaced download rather than trusting the byte count.
        Sha256  = "143827ab00c50e45729d0cdd50ccf242997078f9dd40418113ba395e53194f36"
        Page    = "https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.0"
        Why     = "clang + lld-link -- compiles nxboot.efi (a PE32+ EFI application)"
        Adds    = "C:\Program Files\LLVM\bin"
    },
    @{
        Name    = "QEMU"
        File    = ""            # discovered from the directory listing
        Url     = ""
        Discover = "https://qemu.weilnetz.de/w64/"
        Pattern = 'qemu-w64-setup-(\d{8})\.exe'
        Page    = "https://qemu.weilnetz.de/w64/"
        Why     = "qemu-system-x86_64 -- runs NXSOL; also brings OVMF"
        Adds    = "C:\Program Files\qemu"
    },
    @{
        Name    = "Python"
        File    = "python-3.12.10-amd64.exe"
        Url     = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe"
        Sha256  = ""
        Page    = "https://www.python.org/downloads/release/python-31210/"
        Why     = "runs mkinitramfs.py and mkdisk.py"
        Adds    = ""
    }
)

function Write-Tool([hashtable]$t, [string]$file) {
    $size = ""
    if ($file -and (Test-Path $file)) {
        $size = "  ({0:N1} MB)" -f ((Get-Item $file).Length / 1MB)
    }
    "  {0,-8} {1}{2}" -f $t.Name, $t.Why, $size
}

if ($List) {
    "These will be downloaded into environment\:"
    foreach ($t in $Tools) { Write-Tool $t $null }
    ""
    "OVMF is not downloaded -- the QEMU installer ships it."
    ""
    "Install them yourself afterwards, in this order:"
    "  1. LLVM     (tick 'Add LLVM to the system PATH')"
    "  2. QEMU     (default location)"
    "  3. Python   (tick 'Add python.exe to PATH')"
    exit 0
}

if ($OpenPages) {
    foreach ($t in $Tools) { Start-Process $t.Page }
    exit 0
}

if ($CheckPath) {
    "PATH check -- run this after installing:"
    ""
    $found = @()
    foreach ($cmd in @("clang", "lld-link", "qemu-system-x86_64", "python")) {
        $c = Get-Command $cmd -ErrorAction SilentlyContinue
        if ($c) { "  OK    {0,-20} {1}" -f $cmd, $c.Source }
        else    { "  MISS  {0,-20} not on PATH" -f $cmd }
        $found += $c
    }
    ""
    $qemuShare = "C:\Program Files\qemu\share"
    if (Test-Path $qemuShare) {
        $code = Get-ChildItem $qemuShare -Filter "edk2-x86_64-code.fd" -ErrorAction SilentlyContinue
        $vars = Get-ChildItem $qemuShare -Filter "edk2-i386-vars.fd" -ErrorAction SilentlyContinue
        if ($code) { "  OK    OVMF code        $($code.FullName)" }
        else       { "  MISS  OVMF code        edk2-x86_64-code.fd not found under $qemuShare" }
        if ($vars) { "  OK    OVMF vars        $($vars.FullName)" }
        else       { "  MISS  OVMF vars        edk2-i386-vars.fd not found under $qemuShare" }
    } else {
        "  MISS  OVMF             $qemuShare does not exist (QEMU not installed?)"
    }
    exit 0
}

# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $Env | Out-Null

# Windows 10 ships PowerShell 5.1, which defaults to a TLS version GitHub and
# python.org both refuse. Without this the failure is an opaque socket error.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$problems = @()

foreach ($t in $Tools) {

    # Resolve the URL: fixed, or discovered from the directory listing.
    if ($t.Discover) {
        "--- $($t.Name): looking for the newest build in $($t.Discover)"
        try {
            $html = (Invoke-WebRequest -Uri $t.Discover -UseBasicParsing).Content
            $best = $null
            foreach ($m in [regex]::Matches($html, $t.Pattern)) {
                $name = $m.Value
                $date = $m.Groups[1].Value
                if (-not $best -or $date -gt $best.Date) {
                    $best = @{ Name = $name; Date = $date }
                }
            }
            if (-not $best) { throw "no $($t.Pattern) in the listing" }
            $t.File = $best.Name
            $t.Url  = $t.Discover.TrimEnd("/") + "/" + $best.Name
            "    newest: $($best.Name) (built $($best.Date))"
        } catch {
            "    could not read the listing: $($_.Exception.Message)"
            $problems += "$($t.Name): open $($t.Page) and download it by hand"
            continue
        }
    }

    $dest = Join-Path $Env $t.File
    "--- $($t.Name): $($t.Url)"

    if (Test-Path $dest) {
        "    already present, skipping"
        continue
    }

    try {
        $tmp = "$dest.part"
        Invoke-WebRequest -Uri $t.Url -OutFile $tmp -UseBasicParsing
        Move-Item -Force $tmp $dest
        "    saved: $dest"
    } catch {
        "    download failed: $($_.Exception.Message)"
        if (Test-Path "$dest.part") { Remove-Item -Force "$dest.part" }
        $problems += "$($t.Name): open $($t.Page) and download it by hand"
        continue
    }

    # Verify only when a hash is known. Python's installer is not pinned to
    # one hash here because python.org replaces the file for security
    # releases; a mismatch there would be noise rather than a real signal.
    if ($t.Sha256) {
        $h = (Get-FileHash -Path $dest -Algorithm SHA256).Hash.ToLower()
        if ($h -eq $t.Sha256.ToLower()) {
            "    sha256 OK"
        } else {
            "    sha256 MISMATCH"
            "      expected $($t.Sha256.ToLower())"
            "      got      $h"
            "      deleting -- download this one by hand from $($t.Page)"
            Remove-Item -Force $dest
            $problems += "$($t.Name): sha256 mismatch, fetch by hand"
        }
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
""
"================ installers in $( $Env ) ================"
Get-ChildItem $Env -File -ErrorAction SilentlyContinue |
    ForEach-Object { "  {0,-34} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB) }

if ($problems.Count -gt 0) {
    ""
    "Some could not be fetched automatically. These pages are open now:"
    foreach ($p in $problems) { "  - $p" }
    ""
    "Drop the file into:"
    "  $Env"
    exit 1
}

""
"Nothing has been installed and PATH has not been changed."
"Install in this order, then run:  .\fetch.ps1 -CheckPath"
""
"  1. LLVM-19.1.0-win64.exe"
"       tick 'Add LLVM to the system PATH for all users'"
"  2. qemu-w64-setup-*.exe"
"       accept the default location (C:\Program Files\qemu)"
"       it also installs OVMF under C:\Program Files\qemu\share"
"  3. python-3.12.10-amd64.exe"
"       tick 'Add python.exe to PATH'"
""
"Then open a NEW terminal (setx/PATH changes never apply to the current one)"
"and confirm with:"
"  .\fetch.ps1 -CheckPath"
