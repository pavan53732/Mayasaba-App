Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$storePath = "C:\Users\pavan\PROJECTS\_ui-verify\interaction_walkthrough\store.db"
$binDir = "C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
$exePath = Join-Path $binDir "Mayasaba.exe"

$pinfo = New-Object System.Diagnostics.ProcessStartInfo
$pinfo.FileName = $exePath
$pinfo.WorkingDirectory = $binDir
$pinfo.EnvironmentVariables["MAYASABA_STORE_PATH"] = $storePath
$pinfo.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($pinfo)

Start-Sleep -Seconds 3

try {
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $proc.Id)
    $appWindows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)

    Write-Output "Found $($appWindows.Count) windows for PID $($proc.Id):"
    foreach ($win in $appWindows) {
        Write-Output "  Window: '$($win.Current.Name)' Class: '$($win.Current.ClassName)' BoundingRect: $($win.Current.BoundingRectangle)"
    }
}
finally {
    Stop-Process -Id $proc.Id -Force
}
