# Registers a sign-in task that runs tools/watch-build.sh in WSL five minutes
# after logon, so a new taskbar.dll without published symbols gets a draft PR.
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
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 30)
Register-ScheduledTask -TaskName $name -Action $action -Trigger $trigger -Settings $settings -Force `
    -Description "Opens a draft PR when Windows installs a taskbar.dll the auto-hide mod has no symbols for." | Out-Null
Get-ScheduledTask -TaskName $name | Select-Object TaskName, State
