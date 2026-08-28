# Raise a real Windows toast notification, no modules required.
#
#   powershell -ExecutionPolicy Bypass -File tools\toast.ps1

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType=WindowsRuntime] > $null
$template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent(
    [Windows.UI.Notifications.ToastTemplateType]::ToastText02)
$texts = $template.GetElementsByTagName("text")
$texts.Item(0).AppendChild($template.CreateTextNode("Taskbar auto-hide test")) > $null
$texts.Item(1).AppendChild($template.CreateTextNode("Checking whether the taskbar flaps.")) > $null
$toast = [Windows.UI.Notifications.ToastNotification]::new($template)
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier(
    "Microsoft.Windows.Computer").Show($toast)
Write-Host "Toast raised."
