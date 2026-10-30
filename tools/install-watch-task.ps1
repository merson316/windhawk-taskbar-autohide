# Registers a task that runs tools/watch-build.sh in WSL five minutes after
# logon and then hourly. Windows stages an update's taskbar.dll in WinSxS well
# before it asks to restart, so the hourly run usually has a draft PR ready, or
# a warning raised, before the new taskbar.dll is in use.
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
$triggers = @(
    New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
    New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Hours 1)
)
# A run already in progress (still in its five-minute wait, say) makes the next
# one a no-op, and a run missed while the PC slept happens on waking.
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 30)
Register-ScheduledTask -TaskName $name -Action $action -Trigger $triggers -Settings $settings -Force `
    -Description "Checks running and staged taskbar.dll builds for the auto-hide mod; opens a draft PR or warns." | Out-Null
Get-ScheduledTask -TaskName $name | Select-Object TaskName, State
