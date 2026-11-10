# Tests for tools/install-mod.ps1, the elevated half of the install. Plain
# PowerShell, no Pester: each test throws on failure. They run against a temp
# data folder and HKCU:\Software\TaskbarAutohideTest, never the real Windhawk
# or HKLM, and delete the test key afterwards.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests/install-mod.Tests.ps1
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '..\tools\install-mod.ps1'
$root = 'HKCU:\Software\TaskbarAutohideTest'
$key = "$root\mod"
$mod = 'local@taskbar-autohide-any-edge'
$failed = 0

function Setup {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("taskbar-install-test-" + [guid]::NewGuid())
    $staging = Join-Path $tmp 'staging'
    $data = Join-Path $tmp 'data'
    New-Item -ItemType Directory -Force $staging, "$data\Engine\Mods\64", "$data\ModsSource" | Out-Null
    [IO.File]::WriteAllText("$data\ModsSource\$mod.wh.cpp", "// @version 1.18.0`n")
    [IO.File]::WriteAllText("$data\Engine\Mods\64\${mod}_1.18.0_500841.dll", 'MZ old')
    if (Test-Path $root) { Remove-Item $root -Recurse -Force }
    New-Item $key -Force | Out-Null
    New-ItemProperty $key -Name Version -Value '1.18.0' | Out-Null
    New-ItemProperty $key -Name LibraryFileName -Value "${mod}_1.18.0_500841.dll" | Out-Null
    New-ItemProperty $key -Name Disabled -Value 1 -PropertyType DWord | Out-Null
    New-ItemProperty $key -Name LoggingEnabled -Value 1 -PropertyType DWord | Out-Null
    New-Item "$key\Settings" -Force | Out-Null
    New-ItemProperty "$key\Settings" -Name edge -Value 'top' | Out-Null
    return @{ Tmp = $tmp; Staging = $staging; Data = $data }
}

# A staged request for $version, as windhawk_install.stage leaves it.
function Request($t, $version, $sourceVersion = $version, $dll = 'MZ new') {
    [IO.File]::WriteAllText("$($t.Staging)\request.json", "{`"version`": `"$version`"}")
    [IO.File]::WriteAllText("$($t.Staging)\mod.wh.cpp", "// ==WindhawkMod==`n// @id $mod`n// @version $sourceVersion`n")
    [IO.File]::WriteAllText("$($t.Staging)\mod.dll", $dll)
}

function Run($t) {
    & $script -Staging $t.Staging -DataDir $t.Data -RegKey $key | Out-Null
    return Get-Content -Raw "$($t.Staging)\result.json" | ConvertFrom-Json
}

function Assert($cond, $what) { if (-not $cond) { throw "failed: $what" } }

function Unchanged($t, $r) {
    Assert (-not $r.ok) 'ok is false'
    Assert ($r.error) 'an error is given'
    $p = Get-ItemProperty $key
    Assert ($p.Version -eq '1.18.0') 'Version unchanged'
    Assert ($p.LibraryFileName -eq "${mod}_1.18.0_500841.dll") 'LibraryFileName unchanged'
    Assert (@(Get-ChildItem "$($t.Data)\Engine\Mods\64").Count -eq 1) 'no DLL copied'
    Assert ((Get-Content -Raw "$($t.Data)\ModsSource\$mod.wh.cpp") -match '1\.18\.0') 'source unchanged'
}

function Test($name, [scriptblock]$body) {
    $t = Setup
    try {
        & $body $t
        Write-Host "PASS $name"
    } catch {
        Write-Host "FAIL ${name}: $_"
        $script:failed++
    } finally {
        if (Test-Path $root) { Remove-Item $root -Recurse -Force }
        Remove-Item $t.Tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Test 'Install keeps Disabled and Settings' {
    param($t)
    Request $t '1.19.0'
    $r = Run $t
    Assert $r.ok "ok: $($r.error)"
    $p = Get-ItemProperty $key
    Assert ($p.Version -eq '1.19.0') 'Version is 1.19.0'
    Assert ($p.LibraryFileName -match "^$([regex]::Escape($mod))_1\.19\.0_\d{6}\.dll$") "LibraryFileName $($p.LibraryFileName)"
    Assert ($r.dll -eq $p.LibraryFileName) 'result names the DLL'
    $dll = "$($t.Data)\Engine\Mods\64\$($p.LibraryFileName)"
    Assert ((Get-Content -Raw $dll) -eq 'MZ new') 'DLL copied'
    Assert (Test-Path "$($t.Data)\Engine\Mods\64\${mod}_1.18.0_500841.dll") 'old DLL kept'
    Assert ((Get-Content -Raw "$($t.Data)\ModsSource\$mod.wh.cpp") -match '@version 1\.19\.0') 'source copied'
    Assert ($p.Disabled -eq 1) 'Disabled kept'
    Assert ($p.LoggingEnabled -eq 1) 'LoggingEnabled kept'
    Assert ((Get-ItemProperty "$key\Settings").edge -eq 'top') 'Settings kept'
    Assert (-not (Test-Path "$($t.Staging)\request.json")) 'request consumed'
}

Test 'Refuses older version' {
    param($t)
    Request $t '1.17.0'
    Unchanged $t (Run $t)
}

Test 'Refuses same version' {
    param($t)
    Request $t '1.18.0'
    Unchanged $t (Run $t)
}

Test 'Refuses source version mismatch' {
    param($t)
    Request $t '1.19.0' '1.20.0'
    Unchanged $t (Run $t)
}

Test 'Refuses empty dll' {
    param($t)
    Request $t '1.19.0' '1.19.0' ''
    Unchanged $t (Run $t)
}

Test 'Refuses bad version' {
    param($t)
    Request $t '1.19.0-x'
    Unchanged $t (Run $t)
}

Test 'Refuses missing request' {
    param($t)
    Request $t '1.19.0'
    Remove-Item "$($t.Staging)\request.json"
    Unchanged $t (Run $t)
}

Test 'Puts a newer runtime in place, keeping the old one' {
    param($t)
    $mods = "$($t.Data)\Engine\Mods\64"
    [IO.File]::WriteAllText("$mods\libc++.whl", 'libc++ old')
    [IO.File]::WriteAllText("$mods\libc++_temp1.whl", 'libc++ older')
    [IO.File]::WriteAllText("$mods\libunwind.whl", 'libunwind same')
    Request $t '1.19.0'
    [IO.File]::WriteAllText("$($t.Staging)\libc++.whl", 'libc++ new')
    [IO.File]::WriteAllText("$($t.Staging)\libunwind.whl", 'libunwind same')
    $r = Run $t
    Assert $r.ok "ok: $($r.error)"
    Assert ((Get-Content -Raw "$mods\libc++.whl") -eq 'libc++ new') 'new libc++ in place'
    Assert ((Get-Content -Raw "$mods\libc++_temp2.whl") -eq 'libc++ old') 'old libc++ kept aside'
    Assert ((Get-Content -Raw "$mods\libc++_temp1.whl") -eq 'libc++ older') "Windhawk's own temp kept"
    Assert ((Get-Content -Raw "$mods\libunwind.whl") -eq 'libunwind same') 'libunwind untouched'
    Assert (-not (Test-Path "$mods\libunwind_temp1.whl")) 'same libunwind not moved aside'
}

Test 'Refusal leaves the runtime alone' {
    param($t)
    $mods = "$($t.Data)\Engine\Mods\64"
    Request $t '1.18.0'
    [IO.File]::WriteAllText("$($t.Staging)\libc++.whl", 'libc++ new')
    $r = Run $t
    Assert (-not $r.ok) 'refused'
    Assert (-not (Test-Path "$mods\libc++.whl")) 'runtime copied on a refusal'
}

Test 'A failed runtime copy leaves the installed one in place' {
    param($t)
    $mods = "$($t.Data)\Engine\Mods\64"
    [IO.File]::WriteAllText("$mods\libc++.whl", 'libc++ old')
    Request $t '1.19.0'
    [IO.File]::WriteAllText("$($t.Staging)\libc++.whl", 'libc++ new')
    # Something holds the temporary name open, so the copy fails.
    [IO.File]::WriteAllText("$mods\libc++.whl.tmp", '')
    $lock = [IO.File]::Open("$mods\libc++.whl.tmp", 'Open', 'ReadWrite', 'None')
    try { $r = Run $t } finally { $lock.Dispose() }
    Assert (-not $r.ok) 'refused'
    Assert ($r.error -match 'cannot copy libc\+\+\.whl') "error: $($r.error)"
    Assert ((Get-Content -Raw "$mods\libc++.whl") -eq 'libc++ old') 'installed runtime kept'
    Assert (-not (Test-Path "$mods\libc++_temp1.whl")) 'old runtime moved aside'
    Assert ((Get-ItemProperty $key).Version -eq '1.18.0') 'Version unchanged'
}

Test 'A failed final rename puts the old runtime back' {
    param($t)
    $mods = "$($t.Data)\Engine\Mods\64"
    [IO.File]::WriteAllText("$mods\libc++.whl", 'libc++ old')
    Request $t '1.19.0'
    [IO.File]::WriteAllText("$($t.Staging)\libc++.whl", 'libc++ new')
    # Shadows the cmdlet for the script run from here: moving the new
    # runtime into place fails, every other move goes through.
    function Move-Item {
        param([string]$LiteralPath, [Parameter(Position = 0)][string]$Path,
              [Parameter(Position = 1)][string]$Destination, [switch]$Force)
        if ($LiteralPath -and $Path -and -not $Destination) { $Destination = $Path; $Path = '' }
        $src = if ($LiteralPath) { $LiteralPath } else { $Path }
        if ($src -like '*.whl.tmp') { throw 'injected rename failure' }
        Microsoft.PowerShell.Management\Move-Item -LiteralPath $src -Destination $Destination -Force:$Force
    }
    $r = Run $t
    Assert (-not $r.ok) 'refused'
    Assert ($r.error -match 'cannot put libc\+\+\.whl in place') "error: $($r.error)"
    Assert ((Get-Content -Raw "$mods\libc++.whl") -eq 'libc++ old') 'old runtime back in place'
    Assert (-not (Test-Path "$mods\libc++_temp1.whl")) 'old runtime left aside'
    Assert ((Get-ItemProperty $key).Version -eq '1.18.0') 'Version unchanged'
}

if ($failed) { Write-Host "$failed failed"; exit 1 }
Write-Host 'all passed'
