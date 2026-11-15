# The elevated half of installing a released version in Windhawk. The
# WindhawkTaskbarModInstall task runs it (tools/install-watch-task.ps1
# registers the task) after tools/windhawk_install.py has compiled the release
# into the staging folder:
#
#   request.json   {"version": "1.19.0"}
#   mod.wh.cpp     the tagged source
#   mod.dll        compiled with Windhawk's own compiler
#   libc++.whl, libunwind.whl   only if the compiler's runtime differs from
#                  the one in Engine\Mods\64, which the DLL imports
#
# It refuses obvious mistakes, then copies any staged runtime into
# Engine\Mods\64 (copied in as <name>.whl.tmp, then the one there is
# renamed to <name>_tempN.whl, as Windhawk
# does, since running processes have it loaded; nothing is deleted), then
# copies the DLL and source into Windhawk's
# data folder and sets Version, then LibraryFileName, last of all. Windhawk
# loads the new DLL once LibraryFileName changes. Every other value in the
# mod's key, and its Settings subkey, is left alone, and so is the old DLL.
# The answer goes to result.json: {"ok":true,"dll":...} or {"ok":false,"error":...}.
param(
    [string]$Staging = "$env:LOCALAPPDATA\taskbar-autohide\install",
    [string]$DataDir = "$env:ProgramData\Windhawk",
    [string]$RegKey = 'HKLM:\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-autohide-any-edge'
)
$ErrorActionPreference = 'Stop'
$mod = 'local@taskbar-autohide-any-edge'

function Write-Result($result) {
    $path = Join-Path $Staging 'result.json'
    # Written whole, then renamed: the watcher polls for this file.
    [IO.File]::WriteAllText("$path.tmp", (ConvertTo-Json $result -Compress))
    Move-Item -Force "$path.tmp" $path
}

function NonEmpty($name) {
    $path = Join-Path $Staging $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "$name is missing or empty"
    }
    return $path
}

# A request to change the mod's settings and nothing else:
#   request.json   {"settings": {"hideDelayMs": 800, "hideFully": 0}}
# The mod's settings are whole numbers. Each is written to the Settings subkey
# as Windhawk writes it, and SettingsChangeTime is set, which is how Windhawk
# tells a running mod to read its settings again. Names are checked so that a
# request can reach nothing but a value of that subkey.
function Set-Settings($settings) {
    $pairs = @($settings.PSObject.Properties)
    if ($pairs.Count -eq 0) { throw 'no settings given' }
    foreach ($p in $pairs) {
        if ($p.Name -notmatch '^[A-Za-z][A-Za-z0-9]{0,63}$') { throw "'$($p.Name)' is not a setting name" }
        if ($p.Value -isnot [int] -and $p.Value -isnot [long]) { throw "'$($p.Name)' is not a whole number" }
        if ($p.Value -lt 0 -or $p.Value -gt 1000000) { throw "'$($p.Name)' is not a whole number in range" }
    }
    $key = Join-Path $RegKey 'Settings'
    if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
    foreach ($p in $pairs) {
        New-ItemProperty -LiteralPath $key -Name $p.Name -Value ([int]$p.Value) -PropertyType DWord -Force | Out-Null
    }
    $now = [int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    New-ItemProperty -LiteralPath $RegKey -Name SettingsChangeTime -Value $now -PropertyType DWord -Force | Out-Null
    return "$($pairs.Count) setting(s)"
}

function Install {
    $request = NonEmpty 'request.json'
    $asked = Get-Content -Raw -LiteralPath $request | ConvertFrom-Json
    $version = $asked.version
    # Read once: a second run of the task without a new request refuses.
    Remove-Item -LiteralPath $request
    if ($null -ne $asked.settings) {
        if ($null -ne $version) { throw 'a request is for a version or for settings, not both' }
        return Set-Settings $asked.settings
    }
    $dll = NonEmpty 'mod.dll'
    $source = NonEmpty 'mod.wh.cpp'
    if ("$version" -notmatch '^\d+\.\d+\.\d+$') { throw "bad version '$version'" }
    $installed = (Get-ItemProperty -LiteralPath $RegKey).Version
    if (-not ("$installed" -match '^\d+(\.\d+){1,3}$')) { throw "installed Version '$installed' is not a version" }
    if ([version]$version -le [version]$installed) { throw "$version is not newer than the installed $installed" }
    $m = [regex]::Match((Get-Content -Raw -LiteralPath $source), '(?m)^// @version\s+(\S+)\s*$')
    if (-not $m.Success -or $m.Groups[1].Value -ne $version) {
        throw "the source says @version '$($m.Groups[1].Value)', not $version"
    }

    $mods = Join-Path $DataDir 'Engine\Mods\64'
    foreach ($lib in 'libc++', 'libunwind') {
        $new = Join-Path $Staging "$lib.whl"
        if (-not (Test-Path -LiteralPath $new -PathType Leaf)) { continue }
        $target = Join-Path $mods "$lib.whl"
        if ((Test-Path -LiteralPath $target) -and
            (Get-FileHash -LiteralPath $new).Hash -eq (Get-FileHash -LiteralPath $target).Hash) { continue }
        # Copied in under a temporary name first: if the copy fails, the
        # installed runtime is still in place. Then two renames.
        $incoming = Join-Path $mods "$lib.whl.tmp"
        try {
            Copy-Item -LiteralPath $new $incoming -Force
        } catch {
            Remove-Item -LiteralPath $incoming -Force -ErrorAction SilentlyContinue
            throw "cannot copy $lib.whl into ${mods}: $($_.Exception.Message)"
        }
        $aside = $null
        if (Test-Path -LiteralPath $target) {
            $n = 1
            while (Test-Path -LiteralPath (Join-Path $mods "${lib}_temp$n.whl")) { $n++ }
            $aside = Join-Path $mods "${lib}_temp$n.whl"
            Move-Item -LiteralPath $target $aside
        }
        try {
            Move-Item -LiteralPath $incoming $target
        } catch {
            if ($aside) { Move-Item -LiteralPath $aside $target -ErrorAction SilentlyContinue }
            throw "cannot put $lib.whl in place: $($_.Exception.Message)"
        }
    }
    do {
        $name = "${mod}_${version}_$(Get-Random -Minimum 100000 -Maximum 1000000).dll"
    } while (Test-Path -LiteralPath (Join-Path $mods $name))
    Copy-Item -LiteralPath $dll (Join-Path $mods $name)
    $target = Join-Path $DataDir "ModsSource\$mod.wh.cpp"
    Copy-Item -LiteralPath $source "$target.tmp" -Force
    Move-Item -LiteralPath "$target.tmp" $target -Force

    Set-ItemProperty -LiteralPath $RegKey -Name Version -Value $version
    try {
        Set-ItemProperty -LiteralPath $RegKey -Name LibraryFileName -Value $name
    } catch {
        Set-ItemProperty -LiteralPath $RegKey -Name Version -Value $installed
        throw
    }
    return $name
}

try {
    $name = Install
    Write-Result @{ ok = $true; dll = $name }
} catch {
    Write-Result @{ ok = $false; error = "$($_.Exception.Message)" }
}
