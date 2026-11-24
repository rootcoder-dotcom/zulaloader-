#include <Windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include "shared.h"

#pragma comment(lib, "advapi32.lib")

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

    log("Surucu yukleniyor...");
    if (!install()) { teardown(); system("pause"); return 1; }
    if (!connect())  { teardown(); system("pause"); return 1; }

    log("Zula bekleniyor...");
    DWORD pid = 0;
    for (int i = 0; i < 300 && !pid; i++) { pid = find_pid(ZULA_TARGET); if (!pid) Sleep(500); }
    if (!pid) { log("[-] Zula.exe bulunamadi"); teardown(); system("pause"); return 1; }

    log("[+] Zula.exe pid %lu", pid);
    if (!inject(pid)) { teardown(); system("pause"); return 1; }

    log("[+] INSERT: menu, END: cikis");

    while (GetAsyncKeyState(VK_END) == 0) Sleep(50);

    teardown();
    log("[+] Kapatildi");
    return 0;
}