$storePath = "C:\Users\pavan\PROJECTS\_ui-verify\interaction_walkthrough\store.db"
$binDir = "C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
$exePath = Join-Path $binDir "Mayasaba.exe"

$pinfo = New-Object System.Diagnostics.ProcessStartInfo
$pinfo.FileName = $exePath
$pinfo.WorkingDirectory = $binDir
$pinfo.EnvironmentVariables["MAYASABA_STORE_PATH"] = $storePath
$pinfo.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($pinfo)

Start-Sleep -Seconds 4

try {
    $addType = @"
    using System;
    using System.Runtime.InteropServices;
    using System.Text;

    public class Win32Helper {
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

        [DllImport("user32.dll")]
        public static extern int GetWindowLong(IntPtr hWnd, int nIndex);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT {
            public int Left, Top, Right, Bottom;
        }

        public static void Inspect(uint pid) {
            EnumWindows((hWnd, lParam) => {
                uint p;
                GetWindowThreadProcessId(hWnd, out p);
                if (p == pid) {
                    var title = new StringBuilder(256);
                    GetWindowText(hWnd, title, 256);
                    var cls = new StringBuilder(256);
                    GetClassName(hWnd, cls, 256);
                    bool vis = IsWindowVisible(hWnd);
                    int style = GetWindowLong(hWnd, -16); // GWL_STYLE
                    RECT r;
                    GetWindowRect(hWnd, out r);
                    Console.WriteLine("hWnd: 0x{0:X} Class: {1} Title: '{2}' Vis: {3} Style: 0x{4:X8} Rect: [{5},{6},{7},{8}]",
                        (long)hWnd, cls, title, vis, style, r.Left, r.Top, r.Right, r.Bottom);
                }
                return true;
            }, IntPtr.Zero);
        }
    }
"@
    Add-Type -TypeDefinition $addType
    [Win32Helper]::Inspect($proc.Id)
}
finally {
    if (!$proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
