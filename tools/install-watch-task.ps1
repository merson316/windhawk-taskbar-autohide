# Registers a sign-in task that runs tools/watch-build.sh in WSL two minutes
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
$cmd = "mkdir -p ~/.cache/taskbar-matcher && cd $RepoPath && { date; tools/watch-build.sh; } >> $log 2>&1"
# conhost --headless keeps a console window from flashing up at sign-in.
$action = New-ScheduledTaskAction -Execute "conhost.exe" -Argument "--headless wsl.exe -e bash -lc `"$cmd`""
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$trigger.Delay = "PT2M"
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 30)
Register-ScheduledTask -TaskName $name -Action $action -Trigger $trigger -Settings $settings -Force `
    -Description "Opens a draft PR when Windows installs a taskbar.dll the auto-hide mod has no symbols for." | Out-Null
Get-ScheduledTask -TaskName $name | Select-Object TaskName, State
