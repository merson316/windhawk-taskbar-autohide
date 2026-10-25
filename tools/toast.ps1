# Raise a real Windows toast notification, no modules required.
# From a PowerShell prompt in the repo root:
#
#   .\tools\toast.ps1
#   .\tools\toast.ps1 -Title "Taskbar 10.0.26100.9999" -Message "Draft PR ready to test"

param(
    [string]$Title = "Taskbar auto-hide test",
    [string]$Message = "Checking whether the taskbar flaps."
)

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType=WindowsRuntime] > $null
$template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent(
    [Windows.UI.Notifications.ToastTemplateType]::ToastText02)
$texts = $template.GetElementsByTagName("text")
$texts.Item(0).AppendChild($template.CreateTextNode($Title)) > $null
$texts.Item(1).AppendChild($template.CreateTextNode($Message)) > $null
$toast = [Windows.UI.Notifications.ToastNotification]::new($template)
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier(
    "Microsoft.Windows.Computer").Show($toast)
Write-Host "Toast raised."
