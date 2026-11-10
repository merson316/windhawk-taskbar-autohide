#Requires -RunAsAdministrator
# Registers a task that runs tools/watch-build.sh in WSL at logon, and whenever
# a Windows update has been staged and is waiting for a restart. The new
# taskbar.dll is in WinSxS by then, so a PR that releases itself once CI
# passes is opened, or a warning raised, before the restart puts it in use.
# Also registers WindhawkTaskbarModInstall, which the watcher starts after a
# release to install it in Windhawk (tools/install-mod.ps1, run elevated from
# the staging folder tools/windhawk_install.py fills). Registering an elevated
# task needs an elevated prompt. Security: that task runs a script the owner's
# account can edit, so it protects against mistakes, not against malware that
# already runs as the owner (the owner's decision). To stop a release, convert
# its PR to a draft (closing it works only if its branch is kept):
#
#   .\tools\install-watch-task.ps1
#   .\tools\install-watch-task.ps1 -Uninstall
param(
    [switch]$Uninstall,
    [string]$RepoPath = "~/windhawk-taskbar-autohide"
)

$name = "WindhawkTaskbarBuildWatch"
$installName = "WindhawkTaskbarModInstall"
if ($Uninstall) {
    foreach ($n in $name, $installName) {
        Unregister-ScheduledTask -TaskName $n -Confirm:$false -ErrorAction SilentlyContinue
    }
    return
}
$user = "$env:USERDOMAIN\$env:USERNAME"

$log = "~/.cache/taskbar-matcher/watch.log"
# No fixed wait at logon: the watcher retries its first git fetch until the
# SSH key and the network are ready (see fetch_when_reachable).
$cmd = "mkdir -p ~/.cache/taskbar-matcher && cd $RepoPath && { date; tools/watch-build.sh; } >> $log 2>&1"
# conhost --headless keeps a console window from flashing up at sign-in.
$action = New-ScheduledTaskAction -Execute "conhost.exe" -Argument "--headless wsl.exe -e bash -lc `"$cmd`""
# Servicing event 4, "A reboot is necessary before package ... can be changed
# to the Installed state", is logged once an update's files are staged. The
# Setup log is readable without elevation, so the task needs none.
$staged = Get-CimClass -ClassName MSFT_TaskEventTrigger -Namespace Root/Microsoft/Windows/TaskScheduler |
    New-CimInstance -ClientOnly
$staged.Enabled = $true
$staged.Subscription = @"
<QueryList><Query Id="0" Path="Setup"><Select Path="Setup">*[System[Provider[@Name='Microsoft-Windows-Servicing'] and EventID=4]]</Select></Query></QueryList>
"@
$triggers = @(
    New-ScheduledTaskTrigger -AtLogOn -User $user
    $staged
)
# One update logs event 4 for several packages at once: while a run is in
# progress the others are dropped.
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 30)
Register-ScheduledTask -TaskName $name -Action $action -Trigger $triggers -Settings $settings -Force `
    -Description "Checks running and staged taskbar.dll builds for the auto-hide mod; opens a PR that releases itself once CI passes, or warns." | Out-Null

# The install task: no triggers, the watcher starts it with schtasks /run.
# It runs install-mod.ps1 from %LOCALAPPDATA%, where windhawk_install.py
# copies it from the repo before each install: an elevated process may not
# reach the \\wsl.localhost share. It runs as this user, with highest
# privileges, to write Windhawk's HKLM key and ProgramData folder.
$script = Join-Path $env:LOCALAPPDATA "taskbar-autohide\install\install-mod.ps1"
# conhost --headless, as for the watch task: no console flashes up.
$installAction = New-ScheduledTaskAction -Execute "conhost.exe" `
    -Argument "--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$script`""
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$installSettings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
Register-ScheduledTask -TaskName $installName -Action $installAction -Principal $principal `
    -Settings $installSettings -Force `
    -Description "Installs a released version of the taskbar auto-hide mod in Windhawk; started by the watcher." | Out-Null
Get-ScheduledTask -TaskName $name, $installName | Select-Object TaskName, State
