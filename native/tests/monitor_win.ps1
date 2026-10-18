$storePath = "C:\Users\pavan\PROJECTS\_ui-verify\interaction_walkthrough\store.db"
$binDir = "C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
$exePath = Join-Path $binDir "Mayasaba.exe"

$env:MAYASABA_STORE_PATH = $storePath
$proc = Start-Process -FilePath $exePath -WorkingDirectory $binDir -PassThru

$addType = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class WinWatcher {
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

    public static void Poll(uint pid, int second) {
        EnumWindows((hWnd, lParam) => {
            uint p;
            GetWindowThreadProcessId(hWnd, out p);
            if (p == pid) {
                var cls = new StringBuilder(256);
                GetClassName(hWnd, cls, 256);
                if (cls.ToString() == "WinUIDesktopWin32WindowClass") {
                    var title = new StringBuilder(256);
                    GetWindowText(hWnd, title, 256);
                    bool vis = IsWindowVisible(hWnd);
                    Console.WriteLine("[t={0}s] hWnd=0x{1:X} Title='{2}' Vis={3}", second, (long)hWnd, title, vis);
                }
            }
            return true;
        }, IntPtr.Zero);
    }
}
"@
Add-Type -TypeDefinition $addType

try {
    for ($i = 1; $i -le 15; $i++) {
        Start-Sleep -Seconds 1
        [WinWatcher]::Poll($proc.Id, $i)
    }
}
finally {
    if (!$proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
