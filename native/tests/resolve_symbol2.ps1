$source = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class SymbolResolver2 {
    [DllImport("kernel32.dll")]
    public static extern IntPtr GetCurrentProcess();

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern bool SymInitialize(IntPtr hProcess, string UserSearchPath, bool fInvProcess);

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern ulong SymLoadModuleEx(IntPtr hProcess, IntPtr hFile, string ImageName, string ModuleName, ulong BaseOfDll, uint DllSize, IntPtr Data, uint Flags);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct SYMBOL_INFO {
        public uint SizeOfStruct;
        public uint TypeIndex;
        public ulong Reserved1;
        public ulong Reserved2;
        public uint Index;
        public uint Size;
        public ulong ModBase;
        public uint Flags;
        public ulong Value;
        public ulong Address;
        public uint Register;
        public uint Scope;
        public uint Tag;
        public uint NameLen;
        public uint MaxNameLen;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)]
        public string Name;
    }

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern bool SymFromAddr(IntPtr hProcess, ulong Address, out ulong Displacement, ref SYMBOL_INFO Symbol);

    [StructLayout(LayoutKind.Sequential)]
    public struct IMAGEHLP_LINE64 {
        public uint SizeOfStruct;
        public IntPtr Key;
        public uint LineNumber;
        public IntPtr FileName;
        public ulong Address;
    }

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern bool SymGetLineFromAddr64(IntPtr hProcess, ulong qwAddr, out uint pdwDisplacement, ref IMAGEHLP_LINE64 Line);

    public static void Resolve(string exePath, string pdbDir, ulong rva) {
        IntPtr hProc = GetCurrentProcess();
        if (!SymInitialize(hProc, pdbDir, false)) {
            Console.WriteLine("SymInitialize failed: " + Marshal.GetLastWin32Error());
            return;
        }

        ulong modBase = 0x140000000;
        ulong loaded = SymLoadModuleEx(hProc, IntPtr.Zero, exePath, null, modBase, 0, IntPtr.Zero, 0);
        if (loaded == 0) {
            Console.WriteLine("SymLoadModuleEx failed: " + Marshal.GetLastWin32Error());
            return;
        }

        ulong targetAddr = modBase + rva;
        SYMBOL_INFO sym = new SYMBOL_INFO();
        sym.SizeOfStruct = (uint)Marshal.SizeOf(typeof(SYMBOL_INFO)) - 1024 + 1024;
        sym.MaxNameLen = 1024;
        ulong disp = 0;

        if (SymFromAddr(hProc, targetAddr, out disp, ref sym)) {
            Console.WriteLine("Symbol: {0} (+0x{1:X})", sym.Name, disp);
        } else {
            Console.WriteLine("SymFromAddr failed: " + Marshal.GetLastWin32Error());
        }

        IMAGEHLP_LINE64 line = new IMAGEHLP_LINE64();
        line.SizeOfStruct = (uint)Marshal.SizeOf(typeof(IMAGEHLP_LINE64));
        uint lineDisp = 0;
        if (SymGetLineFromAddr64(hProc, targetAddr, out lineDisp, ref line)) {
            string file = Marshal.PtrToStringAnsi(line.FileName);
            Console.WriteLine("File: {0}:{1}", file, line.LineNumber);
        } else {
            Console.WriteLine("SymGetLineFromAddr64 failed: " + Marshal.GetLastWin32Error());
        }
    }
}
"@

Add-Type -TypeDefinition $source
$exe = "C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release\Mayasaba.exe"
$pdbDir = "C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
[SymbolResolver2]::Resolve($exe, $pdbDir, 0x28bdc)
