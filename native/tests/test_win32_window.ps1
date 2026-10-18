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
    $proc.Refresh()
    Write-Output "PID: $($proc.Id), HasExited: $($proc.HasExited), MainWindowHandle: $($proc.MainWindowHandle)"
    if ($proc.HasExited) {
        Write-Output "ExitCode: $($proc.ExitCode)"
    } else {
        $addType = @"
        using System;
        using System.Collections.Generic;
        using System.Runtime.InteropServices;
        using System.Text;

        public class Win32 {
            public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

            [DllImport("user32.dll")]
            public static extern bool EnumWindows(EnumWindowsProc enumProc, IntPtr lParam);

            [DllImport("user32.dll", SetLastError = true)]
            public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

            [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
            public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

            [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
            public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

            [DllImport("user32.dll")]
            public static extern bool IsWindowVisible(IntPtr hWnd);

            public static List<string> FindWindowsForPid(uint targetPid) {
                var list = new List<string>();
                EnumWindows((hWnd, lParam) => {
                    uint pid;
                    GetWindowThreadProcessId(hWnd, out pid);
                    if (pid == targetPid) {
                        var sbTitle = new StringBuilder(256);
                        GetWindowText(hWnd, sbTitle, 256);
                        var sbClass = new StringBuilder(256);
                        GetClassName(hWnd, sbClass, 256);
                        bool vis = IsWindowVisible(hWnd);
                        list.Add(string.Format("hWnd: 0x{0:X}, Title: '{1}', Class: '{2}', Visible: {3}", (long)hWnd, sbTitle.ToString(), sbClass.ToString(), vis));
                    }
                    return true;
                }, IntPtr.Zero);
                return list;
            }
        }
"@
        Add-Type -TypeDefinition $addType
        $wins = [Win32]::FindWindowsForPid($proc.Id)
        Write-Output "Windows for PID $($proc.Id):"
        $wins | ForEach-Object { Write-Output "  $_" }
    }
}
finally {
    if (!$proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
