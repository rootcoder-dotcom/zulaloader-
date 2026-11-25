#include <Windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include "shared.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static SC_HANDLE g_scm = NULL;
static SC_HANDLE g_svc = NULL;

static void log(const char* fmt, ...)
{
    SYSTEMTIME t;
    va_list a;
    GetLocalTime(&t);
    printf("%02d:%02d:%02d: ", t.wHour, t.wMinute, t.wSecond);
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
    // cikti dosyaya/pipe'a yonlendirildiginde tamponlanir; aninda gorunsun diye flush
    fflush(stdout);
}

static bool privilege(LPCWSTR name)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    bool ok = false;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    if (LookupPrivilegeValueW(NULL, name, &luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), NULL, NULL) != 0;
    }
    CloseHandle(tok);
    return ok;
}

// Eski servis kaydi silinmeyi bekliyorsa (1072) veya yol farki varsa
// kayit yok olana kadar bekleyip yeniden olusturur.
static bool create_service_retry()
{
    for (int i = 0; i < 60; i++) {
        g_svc = CreateServiceW(g_scm, ZULA_SERVICE_NAME, ZULA_SERVICE_NAME,
                               SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
                               SERVICE_ERROR_IGNORE, L"C:\\zula\\zuladrv.sys", NULL, NULL, NULL, NULL, NULL);
        if (g_svc) return true;

        DWORD e = GetLastError();
        if (e == ERROR_SERVICE_EXISTS) {
            g_svc = OpenServiceW(g_scm, ZULA_SERVICE_NAME, SERVICE_ALL_ACCESS);
            if (!g_svc) { log("[-] Servis acilamadi: %lu", GetLastError()); return false; }

            // binPath C:\zula degilse duzelt (eski build\ yolu kalmis olabilir)
            // ChangeServiceConfigW 11 parametre alir: son ikisi ServiceStartName, Password, DisplayName
            if (!ChangeServiceConfigW(g_svc, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
                                      SERVICE_ERROR_IGNORE, L"C:\\zula\\zuladrv.sys",
                                      NULL, NULL, NULL, NULL, NULL, NULL)) {
                DWORD ce = GetLastError();
                if (ce != ERROR_SERVICE_MARKED_FOR_DELETE) {
                    log("[!] Servis yolu degistirilemedi: %lu", ce);
                    return true;
                }
                CloseServiceHandle(g_svc);
                g_svc = NULL;
            } else {
                log("[+] Servis yolu guncellendi: C:\\zula\\zuladrv.sys");
                return true;
            }
        } else if (e != ERROR_SERVICE_MARKED_FOR_DELETE) {
            log("[-] Servis olusturulamadi: %lu", e);
            return false;
        }

        // 1072: silinmeyi bekleyen eski kayit - kisa bekleme ile tekrar dene
        if ((i % 4) == 0) log("[!] Servis silinmeyi bekliyor, tekrar deneniyor... (%d/60)", i / 4 + 1);
        Sleep(500);
    }
    log("[-] Servis kaydi hala silinmedi (1072) - acik handle tutuyor olabilir");
    return false;
}

static bool install()
{
    g_scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!g_scm) { log("[-] SCM acilamadi: %lu", GetLastError()); return false; }

    if (!create_service_retry()) return false;

    if (!StartServiceW(g_svc, 0, NULL)) {
        DWORD e = GetLastError();
        if (e != ERROR_SERVICE_ALREADY_RUNNING) { log("[-] Surucu baslatilamadi: %lu", e); return false; }
    }

    for (int i = 0; i < 50; i++) {
        SERVICE_STATUS st;
        if (QueryServiceStatus(g_svc, &st) && st.dwCurrentState == SERVICE_RUNNING) return true;
        Sleep(100);
    }
    log("[-] Surucu RUNNING durumuna gecmedi");
    return false;
}

static bool connect()
{
    g_dev = CreateFileA(ZULA_WIN32_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_dev == INVALID_HANDLE_VALUE)
        g_dev = CreateFileA(ZULA_GLOBAL_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) { log("[-] Cihaz acilamadi: %lu", GetLastError()); return false; }

    ZULA_VERIFY v{};
    DWORD ret = 0;
    if (!DeviceIoControl(g_dev, IOCTL_ZULA_VERIFY_VERSION, NULL, 0, &v, sizeof(v), &ret, NULL)) {
        log("[-] Surucu dogrulanamadi: %lu", GetLastError());
        return false;
    }
    if (v.Magic != ZULA_MAGIC || v.Version < ZULA_DRIVER_VERSION) {
        log("[-] Surucu surumu uyumsuz (0x%08X)", v.Version);
        return false;
    }
    return true;
}

static bool read_mem(DWORD pid, ULONGLONG addr, void* dst, ULONG size)
{
    std::vector<BYTE> buf(sizeof(ZULA_READ_MEMORY) + size);
    auto* r = (ZULA_READ_MEMORY*)buf.data();
    DWORD ret = 0;

    r->Pid = pid;
    r->Address = addr;
    r->Size = size;

    if (!DeviceIoControl(g_dev, IOCTL_ZULA_READ_MEMORY, buf.data(), sizeof(ZULA_READ_MEMORY),
                         buf.data(), (DWORD)buf.size(), &ret, NULL))
        return false;

    memcpy(dst, r->Data, size);
    return true;
}

static bool write_mem(DWORD pid, ULONGLONG addr, const void* src, ULONG size)
{
    std::vector<BYTE> buf(sizeof(ZULA_WRITE_MEMORY) + size);
    auto* w = (ZULA_WRITE_MEMORY*)buf.data();
    DWORD ret = 0;

    w->Pid = pid;
    w->Address = addr;
    w->Size = size;
    memcpy(w->Data, src, size);

    return DeviceIoControl(g_dev, IOCTL_ZULA_WRITE_MEMORY, buf.data(), (DWORD)buf.size(),
                           NULL, 0, &ret, NULL) != 0;
}

static DWORD find_pid(const char* name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe{ sizeof(pe) };
    DWORD pid = 0;

    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32First(snap, &pe)) {
        do {
            if (!_stricmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool load_file(const char* path, std::vector<BYTE>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

/* ---- hazirlik kapisi: gercek oyun hazir mi? ---- */

struct WndFind { DWORD pid; bool found; };

static BOOL CALLBACK wnd_cb(HWND h, LPARAM l)
{
    auto* p = (WndFind*)l;
    DWORD o = 0;
    GetWindowThreadProcessId(h, &o);
    if (o == p->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
        p->found = true;
        return FALSE;
    }
    return TRUE;
}

// oyun gercekten hazir mi: GameAssembly + ana pencere + BE servisi
// (why: hangi asama eksik, bekleme log'u icin)
static bool game_up(DWORD pid, const char** why)
{
    if (why) *why = NULL;

    bool ga = false;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (s == INVALID_HANDLE_VALUE) {
        if (why) *why = "modul listesi okunamadi (admin gerekebilir)";
        return false;
    }
    MODULEENTRY32 me{ sizeof(me) };
    if (Module32First(s, &me)) do {
        if (!_stricmp(me.szModule, "GameAssembly.dll")) { ga = true; break; }
    } while (Module32Next(s, &me));
    CloseHandle(s);
    if (!ga) {
        if (why) *why = "GameAssembly.dll yuklenmedi";
        return false;
    }

    WndFind w{ pid, false };
    EnumWindows(wnd_cb, (LPARAM)&w);
    if (!w.found) {
        if (why) *why = "oyun penceresi acilmadi";
        return false;
    }

    /* BE servisi: var ama calismiyorsa bekle; yoksa yumusak gecis */
    SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (m) {
        SC_HANDLE b = OpenServiceW(m, L"BEService", SERVICE_QUERY_STATUS);
        if (b) {
            SERVICE_STATUS st;
            bool run = QueryServiceStatus(b, &st) && st.dwCurrentState == SERVICE_RUNNING;
            CloseServiceHandle(b);
            CloseServiceHandle(m);
            if (!run) {
                if (why) *why = "BEService calismiyor";
                return false;
            }
        } else CloseServiceHandle(m);
    }
    return true;
}

// ayni addaki tum surecleri dolas; kapiden gecen ilk gercek oyunu dondur
static DWORD wait_game()
{
    const char* why = NULL;
    for (int i = 0; i < 1200; i++) {          // 10 dk
        DWORD found = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe{ sizeof(pe) };
            if (Process32First(snap, &pe)) {
                do {
                    if (!_stricmp(pe.szExeFile, ZULA_TARGET) &&
                        game_up(pe.th32ProcessID, &why)) {
                        found = pe.th32ProcessID;
                        break;
                    }
                } while (Process32Next(snap, &pe));
            }
            CloseHandle(snap);
        }
        if (found) return found;
        if (GetAsyncKeyState(VK_END) & 0x8000) return 0;   // iptal
        if (why && (i % 60) == 0) log("[*] bekleniyor: %s", why);
        Sleep(500);
    }
    return 0;
}

/* ---- launcher akisi ---- */

// kucuk harfe cevir / icerik ara (DisplayName "Zula" icermeli)
static WCHAR lowc(WCHAR c) { return (c >= L'A' && c <= L'Z') ? (WCHAR)(c + 32) : c; }

static bool wcontains(const WCHAR* hay, const WCHAR* needle)
{
    for (const WCHAR* p = hay; *p; p++) {
        const WCHAR* a = p;
        const WCHAR* b = needle;
        while (*a && *b && lowc(*a) == lowc(*b)) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

static bool find_lnk(WCHAR* out, DWORD cch)
{
    static const WCHAR* roots[2] = {
        L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\MadByte Games\\Zula.lnk",
        L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\MadByte Games\\Zula.lnk"
    };
    for (int i = 0; i < 2; i++) {
        WCHAR ex[MAX_PATH];
        if (!ExpandEnvironmentStringsW(roots[i], ex, MAX_PATH)) continue;
        if (GetFileAttributesW(ex) != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(out, cch, ex);
            return true;
        }
    }
    return false;
}

// Kisayol yoksa: Uninstall kaydindaki InstallLocation (yedek yol)
static bool find_install_dir(WCHAR* out, DWORD cch)
{
    static const WCHAR* keys[2] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    };

    for (int k = 0; k < 2; k++) {
        HKEY h = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keys[k], 0, KEY_READ, &h) != ERROR_SUCCESS)
            continue;

        bool found = false;
        for (DWORD idx = 0; !found; idx++) {
            WCHAR sub[256];
            DWORD slen = 256;
            if (RegEnumKeyExW(h, idx, sub, &slen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;

            HKEY s = NULL;
            if (RegOpenKeyExW(h, sub, 0, KEY_READ, &s) != ERROR_SUCCESS) continue;

            WCHAR disp[256];
            DWORD type = 0, sz = sizeof(disp);
            if (RegQueryValueExW(s, L"DisplayName", NULL, &type, (LPBYTE)disp, &sz) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ) && wcontains(disp, L"Zula")) {
                WCHAR loc[MAX_PATH];
                sz = sizeof(loc);
                type = 0;
                if (RegQueryValueExW(s, L"InstallLocation", NULL, &type, (LPBYTE)loc, &sz) == ERROR_SUCCESS &&
                    (type == REG_SZ || type == REG_EXPAND_SZ) && loc[0]) {
                    wcscpy_s(out, cch, loc);
                    found = true;
                }
            }
            RegCloseKey(s);
        }
        RegCloseKey(h);
        if (found) return true;
    }
    return false;
}

static bool find_exe_in(const WCHAR* dir, const WCHAR* pattern, WCHAR* out, DWORD cch)
{
    WCHAR path[MAX_PATH];
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\%s", dir, pattern);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            _snwprintf_s(out, cch, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
            FindClose(h);
            return true;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return false;
}

static bool launch_launcher()
{
    WCHAR lnk[MAX_PATH];
    WCHAR exe[MAX_PATH] = L"";
    WCHAR dir[MAX_PATH] = L"";
    WCHAR target[MAX_PATH] = L"";
    const WCHAR* file = NULL;

    if (find_lnk(lnk, MAX_PATH)) {
        file = lnk;

        // hedefi coz: sadece dogrulama ve log icin (basarisiz olsa da devam)
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        IShellLinkW* sl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                       IID_IShellLinkW, (void**)&sl))) {
            IPersistFile* pf = nullptr;
            if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
                if (SUCCEEDED(pf->Load(lnk, STGM_READ))) {
                    sl->GetPath(target, MAX_PATH, NULL, SLGP_RAWPATH);
                    sl->GetWorkingDirectory(dir, MAX_PATH);
                }
                pf->Release();
            }
            sl->Release();
        }
        CoUninitialize();
    } else {
        WCHAR inst[MAX_PATH];
        if (find_install_dir(inst, MAX_PATH) &&
            (find_exe_in(inst, L"*launcher*.exe", exe, MAX_PATH) ||
             find_exe_in(inst, L"Zula*.exe", exe, MAX_PATH))) {
            file = exe;
            wcscpy_s(target, MAX_PATH, exe);
            wcscpy_s(dir, MAX_PATH, inst);
        }
    }

    if (!file) { log("[-] Zula kisayolu/kurulumu bulunamadi"); return false; }

    log("[+] hedef: %ls", target[0] ? target : L"(cozulemedi)");

    SHELLEXECUTEINFOW si{ sizeof(si) };
    si.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    si.lpVerb = L"open";
    si.lpFile = file;                                 // once .lnk, yoksa launcher exe
    si.lpDirectory = dir[0] ? dir : NULL;
    si.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&si)) { log("[-] launcher baslatilamadi: %lu", GetLastError()); return false; }
    if (si.hProcess) CloseHandle(si.hProcess);
    return true;
}

static bool inject(DWORD pid)
{
    std::vector<BYTE> dll;
    ULONGLONG remote = 0;
    DWORD ret = 0;

    if (!load_file(ZULA_DLL_PATH, dll)) { log("[-] %s okunamadi", ZULA_DLL_PATH); return false; }

    ZULA_ALLOC_MEMORY alloc{};
    alloc.Pid = pid;
    alloc.Size = dll.size();
    alloc.Protection = PAGE_READWRITE;

    if (!DeviceIoControl(g_dev, IOCTL_ZULA_ALLOC_MEMORY, &alloc, sizeof(alloc), &alloc, sizeof(alloc), &ret, NULL)) {
        log("[-] Bellek ayrilamadi: %lu", GetLastError());
        return false;
    }
    remote = alloc.Address;

    if (!write_mem(pid, remote, dll.data(), (ULONG)dll.size())) {
        log("[-] DLL yazilamadi: %lu", GetLastError());
        return false;
    }

    ZULA_MANUAL_MAP map{};
    map.Pid = pid;
    map.DataVa = remote;
    map.DataSize = dll.size();

    if (!DeviceIoControl(g_dev, IOCTL_ZULA_MANUAL_MAP_DLL, &map, sizeof(map), &map, sizeof(map), &ret, NULL)) {
        log("[-] Manual map basarisiz: %lu", GetLastError());
        return false;
    }

    ZULA_CREATE_THREAD thr{};
    thr.Pid = pid;
    thr.Start = map.Entry;
    thr.Parameter = map.Base;
    thr.CreateSuspended = 0;

    if (!DeviceIoControl(g_dev, IOCTL_ZULA_CREATE_THREAD, &thr, sizeof(thr), NULL, 0, &ret, NULL)) {
        log("[-] Thread olusturulamadi: %lu", GetLastError());
        return false;
    }

    log("[+] Map edildi: base 0x%llX, entry 0x%llX, boyut 0x%llX",
        map.Base, map.Entry, map.ImageSize);
    return true;
}

static void teardown()
{
    if (g_dev != INVALID_HANDLE_VALUE) CloseHandle(g_dev);
    if (g_svc) {
        SERVICE_STATUS st;
        ControlService(g_svc, SERVICE_CONTROL_STOP, &st);
        Sleep(500);
        DeleteService(g_svc);
        CloseServiceHandle(g_svc);
    }
    if (g_scm) CloseServiceHandle(g_scm);
}

int main()
{
    SetConsoleTitleA("zula loader");
    log("Guncelleniyor...");

    privilege(L"SeLoadDriverPrivilege");
    privilege(L"SeDebugPrivilege");

    /* Once mevcut/manuel map'li surucuye baglan (kdmapper once yuklediyse
       install() gerekmez); baglanilamazsa servis kurmayi dene. */
    if (!connect()) {
        log("Surucu yukleniyor...");
        if (!install()) { teardown(); system("pause"); return 1; }
        if (!connect()) { teardown(); system("pause"); return 1; }
    }

    /* Launcher akisi: oyun zaten aciksa atla, degilse kisayoldan baslat.
       wait_game kapisi (GameAssembly + pencere + BE) oyun gercek hazir
       olana kadar enjeksiyonu erteler. */
    if (find_pid(ZULA_TARGET)) {
        log("[+] Zula zaten acik, launcher atlandi");
    } else {
        log("Launcher baslatiliyor...");
        if (!launch_launcher()) log("[!] launcher'i elle ac");
    }

    /* Hazirlik kapisi: launcher/login asamasindaki Zula.exe'ye
       girmemek icin GameAssembly + pencere + BE beklenir; surec
       kapanirsa (launcher yeniden baslatirsa) yeniden enjekte edilir. */
    bool retried = false;
    for (;;) {
        log("Zula bekleniyor...");
        DWORD pid = wait_game();
        if (!pid) {
            if (GetAsyncKeyState(VK_END) & 0x8000) break;
            if (!retried) {                       // launcher kapandiysa bir kez daha dene
                retried = true;
                log("[!] oyun henuz acilmadi, launcher tekrar deneniyor...");
                if (launch_launcher()) continue;
            }
            log("[-] Zula.exe bulunamadi (10 dk)");
            break;
        }

        log("[+] Zula.exe pid %lu", pid);
        if (!inject(pid)) { log("[!] enjeksiyon basarisiz, tekrar deneniyor"); Sleep(2000); continue; }

        log("[+] INSERT: menu, END: cikis");

        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (h) {
            while (WaitForSingleObject(h, 1000) == WAIT_TIMEOUT) {
                if (GetAsyncKeyState(VK_END) & 0x8000) { CloseHandle(h); goto cik; }
            }
            CloseHandle(h);
            log("[!] surec kapandi, yeniden bekleniyor");
        } else {
            log("[!] surec beklenemedi: %lu", GetLastError());
            while ((GetAsyncKeyState(VK_END) & 0x8000) == 0) Sleep(50);
            break;
        }
    }

cik:
    teardown();
    log("[+] Kapatildi");
    return 0;
}