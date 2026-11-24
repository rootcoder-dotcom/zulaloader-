/* zula driver IOCTL test programi.
   7 IOCTL'un tamamini kendi uzerinde dogrular: surum, oku, yaz, modul, alloc,
   thread ve manual map (reloc + import + api-set + koruma + export cagrisi). */
#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "shared.h"

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static int g_fail = 0;
static volatile LONG g_thread_flag = 0;

static void ok(const char* msg)
{
    printf("[OK]   %s\n", msg);
}

static void bad(const char* msg)
{
    printf("[HATA] %s (GetLastError=%lu)\n", msg, GetLastError());
    g_fail++;
}

static DWORD WINAPI tproc(LPVOID p)
{
    (void)p;
    InterlockedExchange(&g_thread_flag, 1);
    return 0;
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
    bool okr = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return okr;
}

/* dosya baytlarini bellek gorunumune kur: RVA'lar dogrudan gecerli olur */
static std::vector<BYTE> build_image(const std::vector<BYTE>& file)
{
    std::vector<BYTE> img;
    if (file.size() < 0x40) return img;
    auto* dos = (IMAGE_DOS_HEADER*)(void*)file.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return img;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > file.size()) return img;
    auto* nt = (IMAGE_NT_HEADERS*)(void*)(file.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return img;

    img.assign(nt->OptionalHeader.SizeOfImage, 0);
    memcpy(img.data(), file.data(), nt->OptionalHeader.SizeOfHeaders);

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!sec[i].SizeOfRawData) continue;
        if ((size_t)sec[i].PointerToRawData + sec[i].SizeOfRawData > file.size()) continue;
        if ((size_t)sec[i].VirtualAddress + sec[i].SizeOfRawData > img.size()) continue;
        memcpy(img.data() + sec[i].VirtualAddress,
               file.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    }
    return img;
}

/* img vektorunden NT header bul (e_lfanew ofsetli) */
static IMAGE_NT_HEADERS* nt_of(const std::vector<BYTE>& img)
{
    if (img.size() < 0x40) return nullptr;
    auto* dos = (IMAGE_DOS_HEADER*)(void*)img.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > img.size()) return nullptr;
    auto* nt = (IMAGE_NT_HEADERS*)(void*)(img.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

/* mapped IAT denetimi: cozulmemis thunk sayisini dondurur */
static int count_unresolved(const std::vector<BYTE>& img, PVOID mapped, int* total, const char* label)
{
    auto* nt = nt_of(img);
    *total = 0;
    if (!nt) return -1;
    auto* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress || dir->VirtualAddress >= img.size()) return -1;

    int unresolved = 0;
    auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(img.data() + dir->VirtualAddress);
    for (; d->Name; d++) {
        if (!d->FirstThunk) continue;
        auto* oft = (IMAGE_THUNK_DATA*)(img.data() + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* ft = (IMAGE_THUNK_DATA*)((BYTE*)mapped + d->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            ULONGLONG v = ft->u1.Function;
            (*total)++;
            /* cozulmus: yuksek kullanici adresi (0x7FF...); cozulmemis: kucuk RVA veya ordinal bayragi */
            if (!(v > 0x10000000ULL && v < 0x0000800000000000ULL)) {
                unresolved++;
                if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal))
                    printf("[DBG]   cozulmedi: %s ordinal %llu\n", label, (unsigned long long)IMAGE_ORDINAL(oft->u1.Ordinal));
                else {
                    auto* ibn = (IMAGE_IMPORT_BY_NAME*)(img.data() + oft->u1.AddressOfData);
                    printf("[DBG]   cozulmedi: %s -> %s!%s\n", label,
                           (const char*)(img.data() + d->Name), ibn->Name);
                }
            }
        }
    }
    return unresolved;
}

/* export adindan RVA bul (bellek gorunumu gorsunden) */
static DWORD find_export_rva(const std::vector<BYTE>& img, const char* name)
{
    auto* nt = nt_of(img);
    if (!nt) return 0;
    auto* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir->VirtualAddress || dir->VirtualAddress >= img.size()) return 0;

    auto* exp = (IMAGE_EXPORT_DIRECTORY*)(img.data() + dir->VirtualAddress);
    auto* names = (DWORD*)(img.data() + exp->AddressOfNames);
    auto* ords  = (WORD*)(img.data() + exp->AddressOfNameOrdinals);
    auto* funcs = (DWORD*)(img.data() + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        if (strcmp((const char*)(img.data() + names[i]), name) == 0)
            return funcs[ords[i]];
    }
    return 0;
}

/* ilk calistirilabilir bolumu bul */
static bool find_exec(const std::vector<BYTE>& img, DWORD* rva)
{
    auto* nt = nt_of(img);
    if (!nt) return false;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            *rva = sec[i].VirtualAddress;
            return true;
        }
    }
    return false;
}

/* SEH sarmalayicilar: C++ nesneleri olmayan fonksiyonlarda kullanilir */
static int try_write(volatile BYTE* p)
{
    __try { *p = (BYTE)(*p); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int try_call_i(int (*fn)(int), int a, int* out)
{
    __try { *out = fn(a); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int try_call_ver(DWORD (*fn)(const char*, unsigned long*), const char* path, unsigned long* out)
{
    __try { *out = fn(path, out); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* manual map testi */
static void test_map(const char* path, const char* label, const char* exportName, int expect)
{
    std::vector<BYTE> file, img;
    DWORD ret = 0, trva = 0;
    int total = 0, unresolved;

    printf("--- map: %s ---\n", label);

    if (!load_file(path, file)) { printf("[HATA] %s okunamadi\n", path); g_fail++; return; }
    img = build_image(file);
    if (img.empty()) { printf("[HATA] %s gorsel kurulamadi\n", path); g_fail++; return; }

    {
        auto* nt = nt_of(img);
        auto& ide = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        auto& ede = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        printf("[DBG] imgsize=0x%X nsec=%u imp=0x%X/0x%X exp=0x%X/0x%X\n",
               (unsigned)img.size(), nt->FileHeader.NumberOfSections,
               ide.VirtualAddress, ide.Size, ede.VirtualAddress, ede.Size);
        if (ide.VirtualAddress && ide.VirtualAddress < img.size()) {
            auto* d0 = (const IMAGE_IMPORT_DESCRIPTOR*)(img.data() + ide.VirtualAddress);
            printf("[DBG] desc0 name=0x%X oft=0x%X ft=0x%X\n",
                   d0->Name, d0->OriginalFirstThunk, d0->FirstThunk);
            if (d0->Name && d0->Name < img.size())
                printf("[DBG] modname=%s\n", (const char*)img.data() + d0->Name);
        }
    }

    ZULA_MANUAL_MAP m{};
    m.Pid = GetCurrentProcessId();
    m.DataVa = (ULONGLONG)(ULONG_PTR)file.data();
    m.DataSize = file.size();

    if (!DeviceIoControl(g_dev, IOCTL_ZULA_MANUAL_MAP_DLL, &m, sizeof(m), &m, sizeof(m), &ret, NULL)) {
        printf("[HATA] map IOCTL basarisiz: %s (err=%lu)\n", label, GetLastError());
        g_fail++;
        return;
    }
    if (!m.Base || !m.ImageSize || m.Entry < m.Base || m.Entry >= m.Base + m.ImageSize) {
        bad("map sonucu gecersiz (base/entry)");
        return;
    }
    printf("[OK]   map: base=0x%llX entry=0x%llX boyut=0x%llX\n", m.Base, m.Entry, m.ImageSize);

    /* driver basliklari silmeli */
    if (*(const USHORT*)(ULONG_PTR)m.Base == IMAGE_DOS_SIGNATURE)
        bad("baslik silinmemis");
    else
        ok("basliklar silindi");

    /* import cozumu */
    unresolved = count_unresolved(img, (PVOID)(ULONG_PTR)m.Base, &total, label);
    if (total <= 0) {
        bad("import dizini okunamadi");
    } else if (unresolved != 0) {
        printf("[HATA] import: %d/%d thunk cozulmedi\n", unresolved, total);
        g_fail++;
    } else {
        printf("[OK]   import: %d/%d thunk cozuldu\n", total, total);
    }

    /* bolum yazma korumasi */
    if (find_exec(img, &trva)) {
        if (try_write((volatile BYTE*)(ULONG_PTR)(m.Base + trva)))
            bad("bolum yazma korumasi uygulanmadi");
        else
            ok("bolum yazma korumasi aktif (.text RX)");
    }

    /* export cagrisi */
    if (exportName) {
        DWORD erva = find_export_rva(img, exportName);
        if (!erva) {
            printf("[HATA] export bulunamadi: %s\n", exportName);
            g_fail++;
        } else if (strcmp(exportName, "GetFileVersionInfoSizeA") == 0) {
            /* dogru imza: DWORD (LPCSTR, LPDWORD) - int(int) ile cagrilirsa AV alir */
            DWORD (*fn)(const char*, unsigned long*) =
                (DWORD(*)(const char*, unsigned long*))(ULONG_PTR)(m.Base + erva);
            unsigned long sz = 0;
            if (!try_call_ver(fn, "C:\\Windows\\System32\\kernel32.dll", &sz)) {
                printf("[HATA] export cagrisi patladi: %s\n", exportName);
                g_fail++;
            } else {
                printf("[OK]   export cagrildi: %s -> boyut=%lu\n", exportName, sz);
            }
        } else {
            int rval = 0;
            int (*fn)(int) = (int(*)(int))(ULONG_PTR)(m.Base + erva);
            if (!try_call_i(fn, 3, &rval)) {
                printf("[HATA] export cagrisi patladi: %s\n", exportName);
                g_fail++;
            } else if (expect != -999 && rval != expect) {
                printf("[HATA] export yanlis dondurdu: %s (beklenen %d, gelen %d)\n",
                       exportName, expect, rval);
                g_fail++;
            } else {
                printf("[OK]   export cagrildi: %s -> %d\n", exportName, rval);
            }
        }
    }
}

int main()
{
    DWORD pid = GetCurrentProcessId();
    DWORD ret = 0;
    int i;

    SetConsoleTitleA("zula driver test");
    printf("=== zula driver IOCTL test (pid %lu) ===\n", pid);

    /* bagimlilik modulleri: api-set cozumleri icin hedef surecte bulunmali */
    HMODULE h1 = LoadLibraryA("vcruntime140.dll");
    HMODULE h2 = LoadLibraryA("vcruntime140_1.dll");
    HMODULE h3 = LoadLibraryA("ucrtbase.dll");
    HMODULE h4 = LoadLibraryA("msvcrt.dll");
    if (!h1 || !h2 || !h3 || !h4) bad("bagimlilik modulleri yuklenemedi");
    else ok("bagimlilik modulleri yuklendi (vcruntime140, vcruntime140_1, ucrtbase, msvcrt)");

    /* cihaz ac */
    g_dev = CreateFileA(ZULA_WIN32_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        bad("cihaz acilamadi (driver yuklu mu? load_driver.bat)");
        printf("\n=== SONUC: BASARISIZ (%d hata) ===\n", g_fail);
        return 1;
    }
    ok("cihaz acildi: " ZULA_WIN32_NAME);

    /* 1 - surum dogrulama */
    ZULA_VERIFY v{};
    if (!DeviceIoControl(g_dev, IOCTL_ZULA_VERIFY_VERSION, NULL, 0, &v, sizeof(v), &ret, NULL))
        bad("VERIFY_VERSION IOCTL");
    else if (v.Magic != ZULA_MAGIC || v.Version != ZULA_DRIVER_VERSION) {
        printf("[HATA] magic/version yanlis: 0x%08X 0x%08X\n", v.Magic, v.Version);
        g_fail++;
    } else
        ok("VERIFY_VERSION: magic=ZULA version=0x00020000");

    /* 2 - kendini oku */
    static BYTE pattern[64];
    for (i = 0; i < 64; i++) pattern[i] = (BYTE)(i ^ 0x5A);
    {
        std::vector<BYTE> buf(sizeof(ZULA_READ_MEMORY) + 64);
        auto* r = (ZULA_READ_MEMORY*)buf.data();
        r->Pid = pid;
        r->Address = (ULONGLONG)(ULONG_PTR)pattern;
        r->Size = 64;
        if (!DeviceIoControl(g_dev, IOCTL_ZULA_READ_MEMORY, buf.data(), sizeof(ZULA_READ_MEMORY),
                             buf.data(), (DWORD)buf.size(), &ret, NULL))
            bad("READ_MEMORY IOCTL");
        else if (memcmp(r->Data, pattern, 64) != 0)
            bad("READ_MEMORY veri uyusmuyor");
        else
            ok("READ_MEMORY: 64 bayt okundu ve dogrulandi");
    }

    /* 3 - kendine yaz */
    static BYTE target[64];
    memset(target, 0, sizeof(target));
    {
        std::vector<BYTE> buf(sizeof(ZULA_WRITE_MEMORY) + 64);
        auto* w = (ZULA_WRITE_MEMORY*)buf.data();
        w->Pid = pid;
        w->Address = (ULONGLONG)(ULONG_PTR)target;
        w->Size = 64;
        memcpy(w->Data, pattern, 64);
        if (!DeviceIoControl(g_dev, IOCTL_ZULA_WRITE_MEMORY, buf.data(), (DWORD)buf.size(),
                             NULL, 0, &ret, NULL))
            bad("WRITE_MEMORY IOCTL");
        else if (memcmp(target, pattern, 64) != 0)
            bad("WRITE_MEMORY veri yazilmadi");
        else
            ok("WRITE_MEMORY: 64 bayt yazildi ve dogrulandi");
    }

    /* 4 - modul bul */
    {
        ZULA_GET_MODULE g{};
        g.Pid = pid;
        lstrcpyW(g.ModuleName, L"kernel32.dll");
        if (!DeviceIoControl(g_dev, IOCTL_ZULA_GET_MODULE, &g, sizeof(g), &g, sizeof(g), &ret, NULL))
            bad("GET_MODULE IOCTL");
        else if (!g.Base || !g.Size)
            bad("GET_MODULE kernel32 bulunamadi");
        else if (*(const USHORT*)(ULONG_PTR)g.Base != IMAGE_DOS_SIGNATURE)
            bad("GET_MODULE base MZ degil");
        else
            ok("GET_MODULE: kernel32.dll bulundu (MZ dogrulandi)");
    }

    /* 5 - bellek ayir + uzerinde yaz/oku */
    ULONGLONG allocAddr = 0;
    {
        ZULA_ALLOC_MEMORY a{};
        a.Pid = pid;
        a.Size = 0x1000;
        a.Protection = PAGE_READWRITE;
        if (!DeviceIoControl(g_dev, IOCTL_ZULA_ALLOC_MEMORY, &a, sizeof(a), &a, sizeof(a), &ret, NULL))
            bad("ALLOC_MEMORY IOCTL");
        else if (!a.Address)
            bad("ALLOC_MEMORY adres donmedi");
        else {
            allocAddr = a.Address;
            printf("[OK]   ALLOC_MEMORY: 0x%llX (0x1000 RW)\n", allocAddr);

            /* uzerine yaz + geri oku (driver kanaliyla) */
            std::vector<BYTE> wbuf(sizeof(ZULA_WRITE_MEMORY) + 64);
            auto* w = (ZULA_WRITE_MEMORY*)wbuf.data();
            w->Pid = pid;
            w->Address = allocAddr;
            w->Size = 64;
            memcpy(w->Data, pattern, 64);
            if (!DeviceIoControl(g_dev, IOCTL_ZULA_WRITE_MEMORY, wbuf.data(), (DWORD)wbuf.size(),
                                 NULL, 0, &ret, NULL)) {
                bad("ALLOC uzerine WRITE basarisiz");
            } else {
                std::vector<BYTE> rbuf(sizeof(ZULA_READ_MEMORY) + 64);
                auto* r = (ZULA_READ_MEMORY*)rbuf.data();
                r->Pid = pid;
                r->Address = allocAddr;
                r->Size = 64;
                if (!DeviceIoControl(g_dev, IOCTL_ZULA_READ_MEMORY, rbuf.data(), sizeof(ZULA_READ_MEMORY),
                                     rbuf.data(), (DWORD)rbuf.size(), &ret, NULL))
                    bad("ALLOC uzerine READ basarisiz");
                else if (memcmp(r->Data, pattern, 64) != 0)
                    bad("ALLOC uzerinde veri uyusmuyor");
                else
                    ok("ALLOC uzerinde yaz/oku turu basarili");
            }
        }
    }

    /* 6 - thread olustur */
    {
        ZULA_CREATE_THREAD t{};
        t.Pid = pid;
        t.Start = (ULONGLONG)(ULONG_PTR)tproc;
        t.CreateSuspended = 0;
        if (!DeviceIoControl(g_dev, IOCTL_ZULA_CREATE_THREAD, &t, sizeof(t), NULL, 0, &ret, NULL))
            bad("CREATE_THREAD IOCTL");
        else {
            for (i = 0; i < 300 && !g_thread_flag; i++) Sleep(10);
            if (g_thread_flag) ok("CREATE_THREAD: thread calisti ve isaretledi");
            else bad("CREATE_THREAD: thread isaret vermedi (3 sn)");
        }
    }

    /* 7 - manual map (uc ornek) */
    test_map("build\\testdep.dll", "testdep.dll (reloc+export)", "zula_probe", 40);
    test_map("C:\\Windows\\System32\\msvcp140.dll", "msvcp140.dll (api-set crt)", NULL, -999);
    test_map("C:\\Windows\\System32\\version.dll", "version.dll (api-set core)", "GetFileVersionInfoSizeA", -999);

    printf("\n=== SONUC: %s (%d hata) ===\n", g_fail ? "BASARISIZ" : "BASARILI", g_fail);
    if (g_dev != INVALID_HANDLE_VALUE) CloseHandle(g_dev);
    return g_fail ? 1 : 0;
}
