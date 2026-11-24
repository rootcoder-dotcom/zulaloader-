/* test hedefi DLL: reloc zorunlu global + export.
   Driver'a map edilince export cagrisi relocations + import + koruma zincirini kanitlar. */
#include <Windows.h>

static int g_buf[4] = { 10, 20, 30, 40 };
static int* g_ptr = g_buf;   /* mutlak isaretci -> base reloc zorunlu */

extern "C" __declspec(dllexport) int zula_probe(int i)
{
    if (i < 0 || i > 3) return -1;
    return g_ptr[i];
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, PVOID p)
{
    (void)h; (void)r; (void)p;
    return TRUE;
}
