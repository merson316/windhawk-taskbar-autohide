# Raise a real Windows toast notification, no modules required.
# From a PowerShell prompt in the repo root:
#
#   .\tools\toast.ps1
#   .\tools\toast.ps1 -Title "Taskbar 10.0.26100.9999" -Message "Draft PR ready to test"

param(
    [string]$Title = "Taskbar auto-hide test",
    [string]$Message = "Checking whether the taskbar flaps."
)

# A toast is shown under the name and icon of the app whose ID raises it. An
# app that is not installed can still have its own: an ID under this key, with
# a display name and an icon, is enough. The icon is copied out of the repo,
# which lives inside WSL and is not there for Windows to read once WSL stops.
$AppId = "merson316.TaskbarAutoHideAnyEdge"
$AppName = "Taskbar auto-hide"
try {
    $dir = Join-Path $env:LOCALAPPDATA "taskbar-autohide-any-edge"
    $icon = Join-Path $dir "toast.png"
    $source = Join-Path $PSScriptRoot "toast.png"
    if (-not (Test-Path $icon) -or (Get-Item $icon).Length -ne (Get-Item $source).Length) {
        New-Item -ItemType Directory -Force -Path $dir > $null
        Copy-Item -Force $source $icon
    }
    $key = "HKCU:\Software\Classes\AppUserModelId\$AppId"
    if (-not (Test-Path $key)) { New-Item -Force -Path $key > $null }
    Set-ItemProperty -Path $key -Name DisplayName -Value $AppName
    Set-ItemProperty -Path $key -Name IconUri -Value $icon
} catch {
    # Without its own identity the toast would not show at all, so fall back to
    # one Windows already knows. It then reads "Microsoft.Windows.Computer".
    $AppId = "Microsoft.Windows.Computer"
}

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType=WindowsRuntime] > $null
$template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent(
    [Windows.UI.Notifications.ToastTemplateType]::ToastText02)
$texts = $template.GetElementsByTagName("text")
$texts.Item(0).AppendChild($template.CreateTextNode($Title)) > $null
$texts.Item(1).AppendChild($template.CreateTextNode($Message)) > $null
$toast = [Windows.UI.Notifications.ToastNotification]::new($template)
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($AppId).Show($toast)
Write-Host "Toast raised."
