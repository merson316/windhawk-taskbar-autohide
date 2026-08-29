// ==WindhawkMod==
// @id              taskbar-autohide-probe
// @name            Taskbar auto-hide probe (diagnostic)
// @description     Reports what the shipping mod sees when it validates taskbar.dll
// @version         1.1.0
// @author          merson316
// @github          https://github.com/merson316
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lshlwapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar auto-hide probe

Diagnostic for "Taskbar auto-hide on any edge" when it reports that this build
of `taskbar.dll` does not match what it was built against.

Installs no hooks and changes no behaviour: it resolves symbols, reads bytes,
writes a report to `%LOCALAPPDATA%\windhawk-autohide-probe.txt`, and stops.
Safe to leave enabled, and safe to run alongside the mod it is diagnosing.

Send that file back. It contains no personal data -- Windows build numbers,
module addresses and machine code from a system DLL.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>

#include <shlobj.h>
#include <tlhelp32.h>
#include <shlwapi.h>

// The shipping mod's expectations, reproduced so the report says whether they
// hold rather than only what the bytes are.
#define EXPECT_SIG_OFFSET 0x19
#define EXPECT_EDGE_OFFSET 0x118
#define EXPECT_FLAGS_OFFSET 0x44

const BYTE kSig[] = {
    0x83, 0xBB, 0x18, 0x01, 0x00, 0x00, 0x03,   // cmpl $0x3,0x118(%rbx)
    0x74, 0x04,                                 // je   .return_real
    0x33, 0xC0,                                 // xor  %eax,%eax
    0xEB, 0x03,                                 // jmp
    0x8B, 0x43, 0x44,                           // mov  0x44(%rbx),%eax
};

HANDLE g_out = INVALID_HANDLE_VALUE;

void Report(PCWSTR format, ...) {
    if (g_out == INVALID_HANDLE_VALUE) {
        return;
    }

    WCHAR line[1024];
    va_list args;
    va_start(args, format);
    wvsprintfW(line, format, args);
    va_end(args);

    lstrcatW(line, L"\r\n");

    char utf8[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8),
                                nullptr, nullptr);
    if (n > 1) {
        DWORD written = 0;
        WriteFile(g_out, utf8, n - 1, &written, nullptr);
    }
}

void ReportBytes(PCWSTR label, const BYTE* p, size_t count) {
    WCHAR line[512];
    for (size_t i = 0; i < count; i += 16) {
        WCHAR* w = line;
        w += wsprintfW(w, L"%s +%02X  ", label, (UINT)i);
        for (size_t j = 0; j < 16 && i + j < count; j++) {
            w += wsprintfW(w, L"%02X ", p[i + j]);
        }
        Report(L"%s", line);
    }
}

// The shipping mod requires this shape at a fixed offset. Scanning for it with
// the two displacement fields wildcarded says whether the shape is still there
// but moved, or the offsets themselves changed -- which decides whether a
// scanning check would fix this or whether the function was rewritten.
void ScanForShape(const BYTE* fn, size_t len) {
    for (size_t i = 0; i + 16 <= len; i++) {
        const BYTE* p = fn + i;
        if (p[0] != 0x83 || p[1] != 0xBB || p[6] != 0x03) {
            continue;
        }
        if (p[7] != 0x74 || p[9] != 0x33 || p[10] != 0xC0 || p[11] != 0xEB) {
            continue;
        }
        if (p[13] != 0x8B || p[14] != 0x43) {
            continue;
        }

        UINT edge = *(const UINT*)(p + 2);
        UINT flags = p[15];
        Report(L"shape found at +%02X  edge_offset=0x%X flags_offset=0x%X",
               (UINT)i, edge, flags);
        Report(L"  expected     at +%02X  edge_offset=0x%X flags_offset=0x%X",
               EXPECT_SIG_OFFSET, EXPECT_EDGE_OFFSET, EXPECT_FLAGS_OFFSET);
        return;
    }
    Report(L"shape NOT found in the first %u bytes", (UINT)len);
}

// Which other Windhawk mods are live in this explorer.exe. A mod that hooked
// the same function first is the leading explanation for identical bytes on
// disk producing a failed check in memory.
void ReportLoadedMods() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        Report(L"could not enumerate loaded modules");
        return;
    }

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (StrStrIW(me.szExePath, L"\\Windhawk\\")) {
                Report(L"  %s", me.szModule);
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
}

// The bytes as loaded, against the bytes on disk. Any difference means
// something patched the function in memory before we looked at it, which is
// the one explanation consistent with a matching file hash.
void ReportDiskComparison(HMODULE loaded, DWORD rva, const BYTE* live,
                          size_t count) {
    HMODULE data = LoadLibraryExW(L"taskbar.dll", nullptr,
                                  LOAD_LIBRARY_AS_DATAFILE);
    if (!data) {
        Report(L"could not map taskbar.dll as data");
        return;
    }

    // LOAD_LIBRARY_AS_DATAFILE returns the base with its low bits flagged.
    const BYTE* base = (const BYTE*)((ULONG_PTR)data & ~(ULONG_PTR)3);
    auto* dos = (const IMAGE_DOS_HEADER*)base;
    auto* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (rva < sec->VirtualAddress ||
            rva >= sec->VirtualAddress + sec->Misc.VirtualSize) {
            continue;
        }

        const BYTE* onDisk =
            base + sec->PointerToRawData + (rva - sec->VirtualAddress);
        if (memcmp(onDisk, live, count) == 0) {
            Report(L"memory matches disk: nothing has patched this function");
        } else {
            Report(L"MEMORY DIFFERS FROM DISK -- the function is patched");
            ReportBytes(L"on disk", onDisk, count);
        }
        FreeLibrary(data);
        return;
    }

    Report(L"rva 0x%X not found in any section", rva);
    FreeLibrary(data);
}

using TrayUI_GetAutoHideFlags_t = UINT(WINAPI*)(void*);
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Address;

using TrayUI_SlideWindow_t = void(WINAPI*)(void*, HWND, const RECT*, HMONITOR,
                                           bool, bool);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Address;

BOOL Wh_ModInit() {
    WCHAR path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                path))) {
        return FALSE;
    }
    lstrcatW(path, L"\\windhawk-autohide-probe.txt");

    g_out = CreateFileW(path, GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_out == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    Report(L"=== taskbar auto-hide probe ===");
    Report(L"pid %lu", GetCurrentProcessId());

    // Build, straight from the kernel rather than the shimmed GetVersion.
    struct MOD_OSVERSIONINFOW {
        ULONG dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion;
        ULONG dwBuildNumber, dwPlatformId;
        WCHAR szCSDVersion[128];
    };
    using RtlGetVersion_t = LONG(WINAPI*)(MOD_OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto p = (RtlGetVersion_t)(void*)GetProcAddress(ntdll,
                                                            "RtlGetVersion")) {
            MOD_OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (p(&vi) == 0) {
                Report(L"windows build %lu", vi.dwBuildNumber);
            }
        }
    }

    HMODULE taskbar =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!taskbar) {
        Report(L"FATAL: taskbar.dll would not load");
        CloseHandle(g_out);
        return FALSE;
    }

    if (GetModuleFileNameW(taskbar, path, ARRAYSIZE(path))) {
        Report(L"taskbar.dll %s", path);
    }
    Report(L"taskbar.dll base %p", (void*)taskbar);

    // Every entry resolves only. Passing no hook function is what keeps this
    // safe to run beside the mod being diagnosed, and is also why the addresses
    // below are the real ones rather than trampolines.
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void) )"},
         &TrayUI_GetAutoHideFlags_Address, nullptr, true},

        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
          LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Address, nullptr, true},
    };

    if (!WindhawkUtils::HookSymbols(taskbar, hooks, ARRAYSIZE(hooks))) {
        Report(L"FATAL: HookSymbols failed -- symbols did not resolve at all");
        CloseHandle(g_out);
        return FALSE;
    }

    Report(L"");
    Report(L"GetAutoHideFlags %p  rva 0x%X",
           (void*)TrayUI_GetAutoHideFlags_Address,
           TrayUI_GetAutoHideFlags_Address
               ? (UINT)((BYTE*)TrayUI_GetAutoHideFlags_Address - (BYTE*)taskbar)
               : 0);
    Report(L"SlideWindow      %p  rva 0x%X", (void*)TrayUI_SlideWindow_Address,
           TrayUI_SlideWindow_Address
               ? (UINT)((BYTE*)TrayUI_SlideWindow_Address - (BYTE*)taskbar)
               : 0);

    if (!TrayUI_GetAutoHideFlags_Address) {
        Report(L"FATAL: GetAutoHideFlags did not resolve");
        CloseHandle(g_out);
        return FALSE;
    }

    const BYTE* fn = (const BYTE*)TrayUI_GetAutoHideFlags_Address;

    Report(L"");
    Report(L"signature match at +%02X: %s", EXPECT_SIG_OFFSET,
           memcmp(fn + EXPECT_SIG_OFFSET, kSig, sizeof(kSig)) == 0 ? L"YES"
                                                                   : L"NO");
    ScanForShape(fn, 0x80);

    Report(L"");
    ReportDiskComparison(taskbar,
                         (DWORD)((const BYTE*)fn - (const BYTE*)taskbar), fn,
                         0x80);

    Report(L"");
    Report(L"windhawk modules loaded in this process:");
    ReportLoadedMods();

    Report(L"");
    ReportBytes(L"GetAutoHideFlags", fn, 0x80);

    Report(L"");
    Report(L"=== end ===");
    CloseHandle(g_out);
    g_out = INVALID_HANDLE_VALUE;
    return TRUE;
}
