#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <mutex>
#include <cstdio>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static HMODULE g_ga = nullptr;
static bool g_open = false, g_ready = false, g_unload = false, g_keyWait = false;
static std::string g_log;
static HWND g_wnd = nullptr;
static WNDPROC g_origProc = nullptr;
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static void* g_presentT = nullptr, *g_presentO = nullptr;
static void* g_resizeT = nullptr, *g_resizeO = nullptr;
using t_present = HRESULT (__stdcall*)(IDXGISwapChain*, UINT, UINT);
using t_resize  = HRESULT (__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, UINT);
static bool g_imInit = false;
static std::mutex g_pm;

static void* g_dom = nullptr;
static void* g_img = nullptr;

using t_dg = void* (*)();
using t_asm = void* (*)(void*, size_t*);
using t_aimg = void* (*)(void*);
using t_imgCnt = size_t (*)(void*);
using t_imgAt = void* (*)(void*, size_t);
using t_clsCnt = size_t (*)(void*);
using t_clsAt = void* (*)(void*, size_t);
using t_clsImg = void* (*)(void*);
using t_tname = const char* (*)(void*);
using t_nspace = const char* (*)(void*);
using t_clsP = void* (*)(void*, const char*, const char*);
using t_ftype = size_t (*)(void*);
using t_vtype = void* (*)(void*);
using t_pcount = int (*)(void*);
using t_pname = const char* (*)(void*, int);
using t_meth = void* (*)(void*, const char*, int);
using t_static = void* (*)(void*);
using t_class = void* (*)(void*, void*);
using t_str = void* (*)(void*);
using t_nstr = void* (*)(void*);

static t_dg p_dg = nullptr;
static t_asm p_asm = nullptr;
static t_aimg p_aimg = nullptr;
static t_imgCnt p_imgCnt = nullptr;
static t_imgAt p_imgAt = nullptr;
static t_clsCnt p_clsCnt = nullptr;
static t_clsAt p_clsAt = nullptr;
static t_clsImg p_clsImg = nullptr;
static t_tname p_tname = nullptr;
static t_nspace p_nspace = nullptr;
static t_clsP p_clsP = nullptr;
static t_ftype p_ftype = nullptr;
static t_vtype p_vtype = nullptr;
static t_pcount p_pcount = nullptr;
static t_pname p_pname = nullptr;
static t_meth p_meth = nullptr;
static t_static p_static = nullptr;
static t_class p_class = nullptr;
static t_str p_str = nullptr;
static t_nstr p_nstr = nullptr;

struct Off { uintptr_t off = 0; int kind = 0; };

static Off g_off;
static std::vector<void*> g_images;
static std::map<std::string, void*> g_classes;
static std::vector<void*> g_players;
static void* g_local = nullptr;
static int g_localTeam = 0;

struct Cfg {
    bool esp = false, teamOnly = false, aim = false, noRecoil = false, ammo = false, speed = false;
    int key = VK_RBUTTON;
    float fov = 8.f, speedVal = 2.5f;
};
static Cfg cfg;

struct V3 { float x, y, z; };
struct M4 { float m[4][4]; };
struct Cam { M4 view, proj; V3 pos; };

static bool w2s(const Cam& c, const V3& p, ImVec2& o)
{
    M4 vp;
    float x, y, z, cw;

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            vp.m[i][j] = 0.f;
            for (int k = 0; k < 4; k++)
                vp.m[i][j] += c.proj.m[i][k] * c.view.m[k][j];
        }

    x = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
    y = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
    z = p.x * vp.m[0][2] + p.y * vp.m[1][2] + p.z * vp.m[2][2] + vp.m[3][2];
    cw = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];

    if (cw < 0.01f) return false;

    o.x = (x / cw * 0.5f + 0.5f) * ImGui::GetIO().DisplaySize.x;
    o.y = (1.f - (y / cw * 0.5f + 0.5f)) * ImGui::GetIO().DisplaySize.y;
    return true;
}

static void Note(const char* fmt, ...)
{
    char b[256];
    va_list a;
    va_start(a, fmt);
    vsprintf_s(b, fmt, a);
    va_end(a);

    g_log += b;
    g_log += "\n";
    if (g_log.size() > 20000) g_log.erase(0, g_log.size() - 20000);

    std::ofstream f("C:\\zula\\cheat.log", std::ios::app);
    if (f) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        f << "[" << t.wHour << ":" << t.wMinute << ":" << t.wSecond << "] " << b << "\n";
    }
}

static std::string i2s(void* s)
{
    if (!s) return "";
    int32_t n = *(int32_t*)((char*)s + 0x10);
    if (n <= 0 || n > 512) return "";
    const wchar_t* w = (const wchar_t*)((char*)s + 0x14);
    int need = WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL);
    std::string out((size_t)need, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, n, out.data(), need, NULL, NULL);
    return out;
}

static bool il2_init()
{
    g_ga = GetModuleHandleA("GameAssembly.dll");
    if (!g_ga) { Note("[-] GameAssembly yok"); return false; }

    p_dg     = (t_dg)GetProcAddress(g_ga, "il2cpp_domain_get");
    p_asm   = (t_asm)GetProcAddress(g_ga, "il2cpp_domain_get_assemblies");
    p_aimg  = (t_aimg)GetProcAddress(g_ga, "il2cpp_assembly_get_image");
    p_imgCnt= (t_imgCnt)GetProcAddress(g_ga, "il2cpp_image_get_class_count");
    p_imgAt = (t_imgAt)GetProcAddress(g_ga, "il2cpp_image_get_class");
    p_clsCnt= (t_clsCnt)GetProcAddress(g_ga, "il2cpp_class_get_class_count");
    p_clsAt = (t_clsAt)GetProcAddress(g_ga, "il2cpp_image_get_class");
    p_clsImg= (t_clsImg)GetProcAddress(g_ga, "il2cpp_class_get_image");
    p_tname = (t_tname)GetProcAddress(g_ga, "il2cpp_class_get_name");
    p_nspace= (t_nspace)GetProcAddress(g_ga, "il2cpp_class_get_namespace");
    p_clsP  = (t_clsP)GetProcAddress(g_ga, "il2cpp_class_from_name");
    p_ftype = (t_ftype)GetProcAddress(g_ga, "il2cpp_field_get_type");
    p_vtype = (t_vtype)GetProcAddress(g_ga, "il2cpp_type_get_object");
    p_pcount= (t_pcount)GetProcAddress(g_ga, "il2cpp_method_get_param_count");
    p_pname = (t_pname)GetProcAddress(g_ga, "il2cpp_method_get_param_name");
    p_meth  = (t_meth)GetProcAddress(g_ga, "il2cpp_class_get_method_from_name");
    p_static= (t_static)GetProcAddress(g_ga, "il2cpp_method_get_param");
    p_class = (t_class)GetProcAddress(g_ga, "il2cpp_object_get_class");
    p_str   = (t_str)GetProcAddress(g_ga, "il2cpp_string_new");
    p_nstr  = (t_nstr)GetProcAddress(g_ga, "il2cpp_object_new");

    if (!p_dg || !p_asm || !p_aimg || !p_clsCnt || !p_clsAt || !p_clsImg || !p_tname || !p_nspace) {
        Note("[-] il2cpp exportlari eksik");
        return false;
    }

    size_t n = 0;
    void* arr = p_asm(p_dg(), &n);
    g_images.clear();
    for (size_t i = 0; i < n; i++) {
        void* img = p_aimg(((void**)arr)[i]);
        if (img) g_images.push_back(img);
    }
    Note("[+] %zu image", g_images.size());
    return !g_images.empty();
}

static void* scan_img(void* img, const char* name)
{
    if (!img) return nullptr;
    size_t n = p_clsCnt(img);
    for (size_t i = 0; i < n; i++) {
        void* c = p_clsAt(img, i);
        if (c && !strcmp(p_tname(c), name)) return c;
    }
    return nullptr;
}

static void* il2_class(const char* ns, const char* name)
{
    if (!name || !*name) return nullptr;

    std::string key = std::string(ns ? ns : "") + "|" + name;
    auto it = g_classes.find(key);
    if (it != g_classes.end()) return it->second;

    void* found = nullptr;
    if (ns && *ns && p_clsP && g_img) found = p_clsP(g_img, ns, name);

    if (!found) {
        for (void* img : g_images) {
            found = scan_img(img, name);
            if (found) break;
        }
    }

    g_classes[key] = found;
    return found;
}

static void* il2_meth(void* cls, const char* name, int argc)
{
    if (!cls || !p_meth) return nullptr;
    void* m = p_meth(cls, name, argc);
    return m ? *(void**)m : nullptr;
}

static int kind_of(void* cls, const char* field)
{
    void* f = nullptr;
    void* t = nullptr;
    void* vt = nullptr;
    const char* tn = nullptr;

    if (!cls || !field || !p_ftype || !p_vtype) return 0;

    f = (void*)p_ftype(cls);   /* doldurulacak */
    (void)f;

    /* alan bul */
    {
        size_t n = p_clsCnt ? 0 : 0;
        (void)n;
    }

    /* il2cpp_field_get_type yerine class_get_field_from_name kullan */
    return 0;
}

static void* find_field(void* cls, const char* name)
{
    /* il2cpp_class_get_fields ile dolas */
    return nullptr;
}

static void resolve();
static void gather();
static void aim();
static void esp();
static void menu();
static void tick();
static void cfg_load();
static void cfg_save();

static void cfg_save()
{
    std::ofstream f("zula.cfg", std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write((const char*)&cfg, sizeof(cfg));
}

static void cfg_load()
{
    std::ifstream f("zula.cfg", std::ios::binary);
    if (!f) return;
    Cfg tmp{};
    f.read((char*)&tmp, sizeof(tmp));
    if (f) cfg = tmp;
}

static void resolve()
{
    /* alan offset'leri il2cpp uzerinden henuz cozulmedi; buton ile yeniden denenir */
    g_off = Off{};
    g_local = nullptr;
    Note("[*] resolve: alan listesi bos (stub)");
}

static void gather()
{
    /* oyuncu listesi il2cpp uzerinden henuz toplanmiyor */
    g_players.clear();
}

static void speed()
{
    if (!g_off.off || !g_local) return;
    float v = cfg.speed ? cfg.speedVal : 1.f;
    *(float*)((char*)g_local + g_off.off) = v;
}

static void no_recoil()
{
    if (!g_off.off || !g_local) return;
    *(float*)((char*)g_local + g_off.off) = 0.f;
}

static void ammo_fix()
{
    if (!g_off.off || !g_local) return;
    *(int*)((char*)g_local + g_off.off) = 999;
}

static void aim()
{
    if (!g_ready || !g_local || !GetAsyncKeyState(cfg.key)) return;
}

static void esp()
{
    if (!g_ready) return;

    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    std::lock_guard<std::mutex> lk(g_pm);

    Cam cam{};
    cam.pos = { 0.f, 0.f, 0.f };

    for (void* p : g_players) {
        V3 head{ 0.f, 0.f, 0.f };
        ImVec2 s{};
        if (!w2s(cam, head, s)) continue;
        d->AddCircle(s, 4.f, IM_COL32(0, 255, 0, 255));
    }
    (void)ds;
}

static void menu()
{
    if (!g_open) return;

    ImGui::SetNextWindowSize({ 480.f, 420.f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("zula", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("t")) {
        if (ImGui::BeginTabItem("Gorsel")) {
            ImGui::Checkbox("ESP", &cfg.esp);
            ImGui::Checkbox("Sadece takim", &cfg.teamOnly);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Nisan")) {
            ImGui::Checkbox("Aimbot", &cfg.aim);
            ImGui::SliderFloat("FOV", &cfg.fov, 1.f, 30.f, "%.1f");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Oyuncu")) {
            ImGui::Checkbox("Sekme yok", &cfg.noRecoil);
            ImGui::Checkbox("Mermi", &cfg.ammo);
            ImGui::Checkbox("Hiz", &cfg.speed);
            ImGui::SliderFloat("Carpan", &cfg.speedVal, 1.f, 8.f, "%.1f");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cozucu")) {
            if (ImGui::Button("Yeniden coz")) { std::lock_guard<std::mutex> lk(g_pm); resolve(); }
            ImGui::BeginChild("log", { 0.f, 0.f }, true);
            ImGui::TextUnformatted(g_log.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

static void tick()
{
    gather();
    if (cfg.speed) speed();
    if (cfg.noRecoil) no_recoil();
    if (cfg.ammo) ammo_fix();
    if (cfg.aim) aim();
}

static LRESULT __stdcall hk_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && w == VK_INSERT && !g_keyWait) {
        g_open = !g_open;
        g_keyWait = true;
        cfg_save();
    }
    if (msg == WM_KEYUP) g_keyWait = false;

    if (g_open && ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return 0;
    return CallWindowProcW(g_origProc, h, msg, w, l);
}

static HRESULT __stdcall hk_resize(IDXGISwapChain* sc, UINT w, UINT h, UINT fmt, UINT flags)
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_ctx) g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return ((t_resize)g_resizeO)(sc, w, h, fmt, flags);
}

static HRESULT __stdcall hk_present(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    if (!g_imInit) {
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev))) {
            g_dev->GetImmediateContext(&g_ctx);

            DXGI_SWAP_CHAIN_DESC d{};
            sc->GetDesc(&d);
            g_wnd = d.OutputWindow;

            g_origProc = (WNDPROC)SetWindowLongPtrA(g_wnd, GWLP_WNDPROC, (LONG_PTR)hk_proc);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            ImGui::StyleColorsDark();
            ImGui_ImplWin32_Init(g_wnd);
            ImGui_ImplDX11_Init(g_dev, g_ctx);

            g_imInit = true;
        }
    }

    if (!g_imInit) return ((t_present)g_presentO)(sc, sync, flags);

    if (!g_rtv) {
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) && bb) {
            g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
            bb->Release();
        }
    }

    if (g_open) tick();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (g_ready) esp();
    menu();
    ImGui::Render();

    if (g_rtv) {
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return ((t_present)g_presentO)(sc, sync, flags);
}

static bool init_dx11()
{
    WNDCLASSEXA wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "zula_dummy";
    RegisterClassExA(&wc);

    HWND w = CreateWindowExA(0, wc.lpszClassName, "zula", WS_OVERLAPPEDWINDOW,
                             0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!w) { Note("[-] dummy pencere"); return false; }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = w;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;

    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx))) {
        Note("[-] D3D11 cihazi olusmadi");
        return false;
    }

    void** vt = *(void***)sc;
    g_presentT = vt[8];
    g_resizeT = vt[13];

    if (MH_CreateHook(g_presentT, hk_present, &g_presentO) != MH_OK) { Note("[-] present hook"); return false; }
    if (MH_CreateHook(g_resizeT, hk_resize, &g_resizeO) != MH_OK) { Note("[-] resize hook"); return false; }
    MH_EnableHook(g_presentT);
    MH_EnableHook(g_resizeT);

    ctx->Release();
    dev->Release();
    sc->Release();
    Note("[+] hooklar kuruldu");
    return true;
}

static void cleanup()
{
    if (g_wnd && g_origProc) SetWindowLongPtrA(g_wnd, GWLP_WNDPROC, (LONG_PTR)g_origProc);
    if (g_presentT) MH_DisableHook(g_presentT);
    if (g_resizeT) MH_DisableHook(g_resizeT);

    if (g_imInit) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imInit = false;
    }
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }

    MH_Uninitialize();
}

static DWORD WINAPI worker(LPVOID)
{
    cfg_load();
    while (!g_unload) {
        if (MH_Initialize() != MH_OK) { Sleep(200); continue; }
        if (init_dx11()) break;
        Sleep(200);
    }

    while (!g_unload) {
        if (!g_ready) { std::lock_guard<std::mutex> lk(g_pm); }
        Sleep(100);
    }

    cleanup();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_unload = true;
        Sleep(300);
    }
    return TRUE;
}