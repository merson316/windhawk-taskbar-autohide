# Registers a task that runs tools/watch-build.sh in WSL five minutes after
# logon, and whenever a Windows update has been staged and is waiting for a
# restart. The new taskbar.dll is in WinSxS by then, so a draft PR is opened,
# or a warning raised, before the restart puts it in use.
# From a PowerShell prompt:
#
#   .\tools\install-watch-task.ps1
#   .\tools\install-watch-task.ps1 -Uninstall
param(
    [switch]$Uninstall,
    [string]$RepoPath = "~/windhawk-taskbar-autohide"
)

$name = "WindhawkTaskbarBuildWatch"
if ($Uninstall) {
    Unregister-ScheduledTask -TaskName $name -Confirm:$false
    return
}

$log = "~/.cache/taskbar-matcher/watch.log"
# The wait runs inside WSL, not on the trigger: WSL may have shut down by the
# time a trigger delay ends, and the watcher's git fetch then races the SSH
# keys still loading in the fresh instance. Starting WSL at logon and waiting
# there gives the keys and the network time to come up first.
$cmd = "sleep 300; mkdir -p ~/.cache/taskbar-matcher && cd $RepoPath && { date; tools/watch-build.sh; } >> $log 2>&1"
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
    New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
    $staged
)
# One update logs event 4 for several packages at once: while a run is in
# progress (or in its five-minute wait) the others are dropped.
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 30)
Register-ScheduledTask -TaskName $name -Action $action -Trigger $triggers -Settings $settings -Force `
    -Description "Checks running and staged taskbar.dll builds for the auto-hide mod; opens a draft PR or warns." | Out-Null
Get-ScheduledTask -TaskName $name | Select-Object TaskName, State
