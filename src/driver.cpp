#include <ntifs.h>
#include <ntimage.h>
#include <ntstrsafe.h>
#include <windef.h>
#include "shared.h"

#pragma warning(disable: 4117)

#define TAG    'luzA'
#define RW_HDR FIELD_OFFSET(ZULA_READ_MEMORY, Data)

/* ntoskrnl exportlari: WDK basliklari bunlari bildirmiyor */
extern "C" NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress,
    PEPROCESS TargetProcess, PVOID TargetAddress, SIZE_T BufferSize,
    KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);

extern "C" PPEB PsGetProcessPeb(PEPROCESS Process);

extern "C" NTSTATUS ZwProtectVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress,
    PSIZE_T RegionSize, ULONG NewProtect, PULONG OldProtect);

extern "C" PIMAGE_NT_HEADERS RtlImageNtHeader(PVOID Base);

typedef struct _MY_LDR_ENTRY {
    LIST_ENTRY     InLoadOrderLinks;
    LIST_ENTRY     InMemoryOrderLinks;
    LIST_ENTRY     InInitializationOrderLinks;
    PVOID          DllBase;
    PVOID          EntryPoint;
    ULONG          SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} MY_LDR_ENTRY;

typedef struct _MY_PEB {
    BYTE    Reserved1[2];
    BYTE    BeingDebugged;
    BYTE    Reserved2[1];
    PVOID   Reserved3[2];
    PVOID   Ldr;
} MY_PEB;

typedef struct _MY_PEB_LDR {
    ULONG      Length;
    BOOLEAN    Initialized;
    PVOID      SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
} MY_PEB_LDR;

typedef struct _DRV { PDEVICE_OBJECT Device; } DRV;

static DRV* g_drv = NULL;

extern "C" NTSTATUS NTAPI RtlCreateUserThread(HANDLE ProcessHandle, PSECURITY_DESCRIPTOR Sd,
    BOOLEAN CreateSuspended, ULONG StackZeroBits, SIZE_T StackReserve,
    SIZE_T StackCommit, PVOID Start, PVOID Parameter, PHANDLE Thread, PCLIENT_ID Cid);

/* kullanici modu erisim sabitleri kernel basliklarinda tanimli degil */
#ifndef PROCESS_CREATE_THREAD
#define PROCESS_CREATE_THREAD      0x0002
#define PROCESS_QUERY_INFORMATION  0x0400
#define PROCESS_VM_OPERATION       0x0008
#define PROCESS_VM_READ            0x0010
#define PROCESS_VM_WRITE           0x0020
#endif

/* ASCII buyuk/kucuk harf duyarsiz eslestirme (PEB adlari import adiyla ayni yazimda olmayabilir) */
static WCHAR wlwr(WCHAR c)
{
    return (c >= L'A' && c <= L'Z') ? (WCHAR)(c + 32) : c;
}

static BOOLEAN weq(const WCHAR* a, const WCHAR* b)
{
    while (*a && wlwr(*a) == wlwr(*b)) { a++; b++; }
    return *a == *b;
}

static NTSTATUS rw_copy(PEPROCESS proc, BOOLEAN write, PVOID local, PVOID remote, SIZE_T size)
{
    SIZE_T done = 0;
    if (!size || size > 0x20000000) return STATUS_INVALID_PARAMETER;
    if (write)
        return MmCopyVirtualMemory(PsGetCurrentProcess(), local, proc, remote, size, KernelMode, &done);
    return MmCopyVirtualMemory(proc, remote, PsGetCurrentProcess(), local, size, KernelMode, &done);
}

static PVOID mod_base(PEPROCESS proc, const WCHAR* name)
{
    KAPC_STATE apc;
    PVOID base = NULL;
    KeStackAttachProcess(proc, &apc);

    /* __try YOK: kdmapper ile pool'a map edilmis image'in .pdata'si
       inverted function table'de kayitli degildir, exception dispatch
       edilemez -> bugcheck. Bunun yerine MmIsAddressValid ile acik kontrol. */
    MY_PEB* peb = (MY_PEB*)PsGetProcessPeb(proc);
    if (peb && MmIsAddressValid(peb)) {
        MY_PEB_LDR* ldr = (MY_PEB_LDR*)peb->Ldr;
        if (ldr && MmIsAddressValid(ldr)) {
            PLIST_ENTRY head = &ldr->InLoadOrderModuleList;
            for (PLIST_ENTRY e = ldr->InLoadOrderModuleList.Flink;
                 e && e != head && MmIsAddressValid(e); e = e->Flink) {
                MY_LDR_ENTRY* ent = CONTAINING_RECORD(e, MY_LDR_ENTRY, InLoadOrderLinks);
                if (!MmIsAddressValid(ent)) break;
                if (ent->BaseDllName.Buffer && MmIsAddressValid(ent->BaseDllName.Buffer) &&
                    weq(ent->BaseDllName.Buffer, name)) {
                    base = ent->DllBase;
                    break;
                }
            }
        }
    }
    KeUnstackDetachProcess(&apc);
    return base;
}

static BOOLEAN ansi_eq(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static BOOLEAN wprefix(const WCHAR* s, const WCHAR* p)
{
    while (*p) { if (*s++ != *p++) return FALSE; }
    return TRUE;
}

/* Modul adini (import tanimi ya da forwarder parcasi) PEV tablosunda ara.
   API set contract'lari ucrtbase/kernelbase uzerinden cozulur. */
static PVOID name_mod(PEPROCESS proc, const char* name)
{
    WCHAR w[128];
    ULONG i, n = 0;

    /* kucuk harfe cevir */
    for (i = 0; name[i] && n < 126; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        w[n++] = (WCHAR)(UCHAR)c;
    }
    w[n] = 0;

    /* forwarder adlarinda ".dll" soneki olmayabilir */
    if (n < 4 || w[n - 4] != L'.' || w[n - 3] != L'd' || w[n - 2] != L'l' || w[n - 1] != L'l') {
        if (n < 123) {
            w[n++] = L'.'; w[n++] = L'd'; w[n++] = L'l'; w[n++] = L'l'; w[n] = 0;
        }
    }

    if (wprefix(w, L"api-ms-win-crt-"))  return mod_base(proc, L"ucrtbase.dll");
    if (wprefix(w, L"api-ms-win-core-rtlsupport-")) return mod_base(proc, L"ntdll.dll");
    if (wprefix(w, L"api-ms-win-core-")) return mod_base(proc, L"kernelbase.dll");
    return mod_base(proc, w);
}

/* Export tablosundan fonksiyon bul; forwarder'lari recursive cozer */
static PVOID find_export(PEPROCESS proc, PVOID base, const char* name, ULONG ord, ULONG depth)
{
    PIMAGE_NT_HEADERS nt;
    PIMAGE_DATA_DIRECTORY ed;
    PIMAGE_EXPORT_DIRECTORY exp;
    PULONG funcs, names;
    PUSHORT ords;
    ULONG i, rva = 0;

    if (!base || depth > 4) return NULL;

    nt = RtlImageNtHeader(base);
    if (!nt) return NULL;
    ed = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!ed->Size) return NULL;

    exp = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + ed->VirtualAddress);
    funcs = (PULONG)((PUCHAR)base + exp->AddressOfFunctions);
    names = (PULONG)((PUCHAR)base + exp->AddressOfNames);
    ords  = (PUSHORT)((PUCHAR)base + exp->AddressOfNameOrdinals);

    if (name) {
        for (i = 0; i < exp->NumberOfNames; i++) {
            if (ansi_eq((const char*)((PUCHAR)base + names[i]), name)) {
                rva = funcs[ords[i]];
                break;
            }
        }
    } else if (ord >= exp->Base && (ord - exp->Base) < exp->NumberOfFunctions) {
        rva = funcs[ord - exp->Base];
    }
    if (!rva) return NULL;

    /* forwarder: "MOD.func" veya "MOD.#123" */
    if (rva >= ed->VirtualAddress && rva < ed->VirtualAddress + ed->Size) {
        const char* fwd = (const char*)((PUCHAR)base + rva);
        char modn[96];
        const char* dot = fwd;
        ULONG k = 0;
        PVOID fm;

        while (*dot && *dot != '.' && k < 95) modn[k++] = *dot++;
        if (*dot != '.') return NULL;
        modn[k] = 0;

        fm = name_mod(proc, modn);
        if (!fm) return NULL;

        if (dot[1] == '#') {
            ULONG o = 0;
            const char* p = dot + 2;
            while (*p >= '0' && *p <= '9' && o < 0x10000) o = o * 10 + (ULONG)(*p++ - '0');
            return find_export(proc, fm, NULL, o, depth + 1);
        }
        return find_export(proc, fm, dot + 1, 0, depth + 1);
    }

    return (PVOID)((PUCHAR)base + rva);
}

static NTSTATUS do_alloc(PEPROCESS proc, ULONGLONG size, ULONG prot, PULONGLONG out)
{
    KAPC_STATE apc;
    PVOID base = NULL;
    SIZE_T sz;
    NTSTATUS st;

    if (!size || size > 0x10000000) return STATUS_INVALID_PARAMETER;
    sz = (SIZE_T)size;

    KeStackAttachProcess(proc, &apc);
    st = ZwAllocateVirtualMemory(ZwCurrentProcess(), &base, 0, &sz, MEM_COMMIT | MEM_RESERVE, prot);
    KeUnstackDetachProcess(&apc);

    if (NT_SUCCESS(st)) *out = (ULONGLONG)base;
    return st;
}

static void unlink(PVOID image)
{
    MY_PEB* peb = (MY_PEB*)PsGetProcessPeb(PsGetCurrentProcess());
    if (!peb || !peb->Ldr) return;
    MY_PEB_LDR* ldr = (MY_PEB_LDR*)peb->Ldr;
    for (PLIST_ENTRY e = ldr->InLoadOrderModuleList.Flink;
         e != &ldr->InLoadOrderModuleList; e = e->Flink) {
        MY_LDR_ENTRY* ent = CONTAINING_RECORD(e, MY_LDR_ENTRY, InLoadOrderLinks);
        if (ent->DllBase == image) {
            RemoveEntryList(&ent->InLoadOrderLinks);
            RemoveEntryList(&ent->InMemoryOrderLinks);
            RemoveEntryList(&ent->InInitializationOrderLinks);
            return;
        }
    }
}

static NTSTATUS map_dll(ZULA_MANUAL_MAP* m)
{
    PEPROCESS proc = NULL;
    PVOID kbuf = NULL, base = NULL;
    SIZE_T size;
    NTSTATUS st;
    KAPC_STATE apc;
    PIMAGE_NT_HEADERS nt;
    PIMAGE_SECTION_HEADER sec;
    ULONG i, nsec;

    st = PsLookupProcessByProcessId(ULongToHandle(m->Pid), &proc);
    if (!NT_SUCCESS(st)) return st;

    kbuf = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)m->DataSize, TAG);
    if (!kbuf) { ObDereferenceObject(proc); return STATUS_INSUFFICIENT_RESOURCES; }

    st = STATUS_SUCCESS;
    /* __try YOK (kdmapper: exception dispatch edilemez) - acik kontrol */
    if (!m->DataVa || !m->DataSize || m->DataSize > 0x10000000 ||
        !MmIsAddressValid((PVOID)m->DataVa) ||
        !MmIsAddressValid((PVOID)((PUCHAR)m->DataVa + m->DataSize - 1))) {
        st = STATUS_ACCESS_VIOLATION;
    } else {
        RtlCopyMemory(kbuf, (PVOID)m->DataVa, (SIZE_T)m->DataSize);
    }
    if (!NT_SUCCESS(st)) goto out;

    nt = RtlImageNtHeader(kbuf);
    if (!nt) { st = STATUS_INVALID_IMAGE_FORMAT; goto out; }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) { st = STATUS_INVALID_IMAGE_FORMAT; goto out; }

    size = nt->OptionalHeader.SizeOfImage;
    if (size < nt->OptionalHeader.SizeOfHeaders || size > 0x10000000) { st = STATUS_INVALID_IMAGE_FORMAT; goto out; }

    st = do_alloc(proc, size, PAGE_EXECUTE_READWRITE, (PULONGLONG)&base);
    if (!NT_SUCCESS(st)) goto out;

    /* hedef process baglaminda kopya + relocate + import + TLS
       __try YOK (kdmapper: exception dispatch edilemez) - adreslerin
       tamami gecerli: kbuf NonPagedPool, base ZwAllocateVirtualMemory,
       nt kbuf uzerinde. */
    KeStackAttachProcess(proc, &apc);
    {
        RtlCopyMemory(base, kbuf, nt->OptionalHeader.SizeOfHeaders);
        sec = IMAGE_FIRST_SECTION(nt);
        nsec = nt->FileHeader.NumberOfSections;
        for (i = 0; i < nsec; i++) {
            if (!sec[i].SizeOfRawData) continue;
            if (sec[i].PointerToRawData + sec[i].SizeOfRawData > (ULONG)m->DataSize) continue;
            RtlCopyMemory((PUCHAR)base + sec[i].VirtualAddress,
                          (PUCHAR)kbuf + sec[i].PointerToRawData,
                          min(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize));
        }

        /* relocations */
        {
            PIMAGE_DATA_DIRECTORY rd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            ULONGLONG delta = (ULONGLONG)base - nt->OptionalHeader.ImageBase;
            if (rd->Size && delta) {
                PIMAGE_BASE_RELOCATION rel = (PIMAGE_BASE_RELOCATION)((PUCHAR)base + rd->VirtualAddress);
                ULONG guard = 0;
                while (rel->SizeOfBlock && rel->VirtualAddress && guard++ < 4096) {
                    ULONG cnt = (rel->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(USHORT);
                    PUSHORT ent = (PUSHORT)(rel + 1);
                    for (i = 0; i < cnt; i++) {
                        if ((ent[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                            PULONGLONG f = (PULONGLONG)((PUCHAR)base + rel->VirtualAddress + (ent[i] & 0xFFF));
                            *f += delta;
                        }
                    }
                    rel = (PIMAGE_BASE_RELOCATION)((PUCHAR)rel + rel->SizeOfBlock);
                }
            }
        }

        /* imports (forwarder + api-set cozumlemeli) */
        {
            PIMAGE_DATA_DIRECTORY id = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (id->Size) {
                PIMAGE_IMPORT_DESCRIPTOR d = (PIMAGE_IMPORT_DESCRIPTOR)((PUCHAR)base + id->VirtualAddress);
                for (; d->Name; d++) {
                    PVOID mod;
                    PIMAGE_THUNK_DATA oft, ft;

                    mod = name_mod(proc, (const char*)((PUCHAR)base + d->Name));
                    if (!mod) continue;

                    oft = (PIMAGE_THUNK_DATA)((PUCHAR)base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
                    ft = (PIMAGE_THUNK_DATA)((PUCHAR)base + d->FirstThunk);

                    for (; oft->u1.AddressOfData; oft++, ft++) {
                        PVOID fn;
                        if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) {
                            fn = find_export(proc, mod, NULL, (ULONG)IMAGE_ORDINAL(oft->u1.Ordinal), 0);
                        } else {
                            PIMAGE_IMPORT_BY_NAME ibn = (PIMAGE_IMPORT_BY_NAME)((PUCHAR)base + oft->u1.AddressOfData);
                            fn = find_export(proc, mod, (const char*)ibn->Name, 0, 0);
                        }
                        if (fn) ft->u1.Function = (ULONG_PTR)fn;
                    }
                }
            }
        }

        /* TLS callbacks */
        {
            PIMAGE_DATA_DIRECTORY td = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
            if (td->Size) {
                PIMAGE_TLS_DIRECTORY tls = (PIMAGE_TLS_DIRECTORY)((PUCHAR)base + td->VirtualAddress);
                PULONGLONG cb = (PULONGLONG)tls->AddressOfCallBacks;
                if (cb) {
                    for (; *cb; cb++)
                        ((void(NTAPI*)(PVOID, ULONG, PVOID))*cb)(base, 1, NULL);
                }
            }
        }
    }
    KeUnstackDetachProcess(&apc);
    if (!NT_SUCCESS(st)) goto out;

    /* section bazli koruma */
    {
        sec = IMAGE_FIRST_SECTION(nt);
        for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            ULONG ch = sec[i].Characteristics;
            ULONG prot = PAGE_READONLY;
            PVOID a;
            SIZE_T s;

            if (ch & IMAGE_SCN_MEM_EXECUTE)
                prot = (ch & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
            else if (ch & IMAGE_SCN_MEM_WRITE)
                prot = PAGE_READWRITE;

            a = (PVOID)((PUCHAR)base + sec[i].VirtualAddress);
            s = sec[i].Misc.VirtualSize;

            KeStackAttachProcess(proc, &apc);
            ZwProtectVirtualMemory(ZwCurrentProcess(), &a, &s, prot, NULL);
            KeUnstackDetachProcess(&apc);
        }
    }

    /* PEB unlink + header silme */
    KeStackAttachProcess(proc, &apc);
    unlink(base);
    RtlZeroMemory(base, nt->OptionalHeader.SizeOfHeaders);
    KeUnstackDetachProcess(&apc);

    KeInvalidateAllCaches();

    m->Base = (ULONGLONG)base;
    m->Entry = (ULONGLONG)base + nt->OptionalHeader.AddressOfEntryPoint;
    m->ImageSize = size;

out:
    if (kbuf) ExFreePoolWithTag(kbuf, TAG);
    ObDereferenceObject(proc);
    return st;
}

static NTSTATUS CC(PDEVICE_OBJECT dev, PIRP irp)
{
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(irp);
    PVOID  buf    = irp->AssociatedIrp.SystemBuffer;
    ULONG  inLen  = s->Parameters.DeviceIoControl.InputBufferLength;
    ULONG  outLen = s->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG  code   = s->Parameters.DeviceIoControl.IoControlCode;
    ULONG  out    = 0;
    NTSTATUS st   = STATUS_SUCCESS;
    PEPROCESS proc = NULL;

    UNREFERENCED_PARAMETER(dev);

    if (s->MajorFunction == IRP_MJ_CREATE || s->MajorFunction == IRP_MJ_CLOSE) goto done;
    if (s->MajorFunction != IRP_MJ_DEVICE_CONTROL) { st = STATUS_INVALID_DEVICE_REQUEST; goto done; }
    if (!buf) { st = STATUS_INVALID_PARAMETER; goto done; }

    switch (code) {

    case IOCTL_ZULA_VERIFY_VERSION: {
        ZULA_VERIFY* v;
        if (outLen < sizeof(ZULA_VERIFY)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        v = (ZULA_VERIFY*)buf;
        v->Magic = ZULA_MAGIC;
        v->Version = ZULA_DRIVER_VERSION;
        out = sizeof(ZULA_VERIFY);
        break;
    }

    case IOCTL_ZULA_READ_MEMORY: {
        ZULA_READ_MEMORY* r;
        ULONG cap;
        ULONGLONG n;
        if (inLen < RW_HDR) { st = STATUS_BUFFER_TOO_SMALL; break; }
        r = (ZULA_READ_MEMORY*)buf;
        cap = outLen > RW_HDR ? outLen - RW_HDR : 0;
        n = min(r->Size, (ULONGLONG)cap);
        if (!n) { st = STATUS_BUFFER_TOO_SMALL; break; }
        st = PsLookupProcessByProcessId(ULongToHandle(r->Pid), &proc);
        if (!NT_SUCCESS(st)) break;
        st = rw_copy(proc, FALSE, r->Data, (PVOID)r->Address, (SIZE_T)n);
        ObDereferenceObject(proc);
        if (NT_SUCCESS(st)) out = RW_HDR + (ULONG)n;
        break;
    }

    case IOCTL_ZULA_WRITE_MEMORY: {
        ZULA_WRITE_MEMORY* w;
        if (inLen < RW_HDR) { st = STATUS_BUFFER_TOO_SMALL; break; }
        w = (ZULA_WRITE_MEMORY*)buf;
        if (inLen < RW_HDR + w->Size) { st = STATUS_BUFFER_TOO_SMALL; break; }
        st = PsLookupProcessByProcessId(ULongToHandle(w->Pid), &proc);
        if (!NT_SUCCESS(st)) break;
        st = rw_copy(proc, TRUE, w->Data, (PVOID)w->Address, (SIZE_T)w->Size);
        ObDereferenceObject(proc);
        break;
    }

    case IOCTL_ZULA_GET_MODULE: {
        ZULA_GET_MODULE* g;
        PIMAGE_NT_HEADERS nt;
        if (inLen < sizeof(ZULA_GET_MODULE) || outLen < sizeof(ZULA_GET_MODULE)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        g = (ZULA_GET_MODULE*)buf;
        g->ModuleName[63] = 0;
        st = PsLookupProcessByProcessId(ULongToHandle(g->Pid), &proc);
        if (!NT_SUCCESS(st)) break;
        g->Base = (ULONGLONG)mod_base(proc, g->ModuleName);
        ObDereferenceObject(proc);
        if (!g->Base) { st = STATUS_NOT_FOUND; break; }
        nt = RtlImageNtHeader((PVOID)g->Base);
        g->Size = nt ? nt->OptionalHeader.SizeOfImage : 0;
        out = sizeof(ZULA_GET_MODULE);
        break;
    }

    case IOCTL_ZULA_ALLOC_MEMORY: {
        ZULA_ALLOC_MEMORY* a;
        if (inLen < sizeof(ZULA_ALLOC_MEMORY) || outLen < sizeof(ZULA_ALLOC_MEMORY)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        a = (ZULA_ALLOC_MEMORY*)buf;
        st = PsLookupProcessByProcessId(ULongToHandle(a->Pid), &proc);
        if (!NT_SUCCESS(st)) break;
        st = do_alloc(proc, a->Size, a->Protection, &a->Address);
        ObDereferenceObject(proc);
        if (NT_SUCCESS(st)) out = sizeof(ZULA_ALLOC_MEMORY);
        break;
    }

    case IOCTL_ZULA_CREATE_THREAD: {
        ZULA_CREATE_THREAD* t;
        HANDLE h = NULL, hp = NULL;
        if (inLen < sizeof(ZULA_CREATE_THREAD)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        t = (ZULA_CREATE_THREAD*)buf;
        st = PsLookupProcessByProcessId(ULongToHandle(t->Pid), &proc);
        if (!NT_SUCCESS(st)) break;
        /* RtlCreateUserThread prosesi HANDLE olarak bekler, PEPROCESS degil */
        st = ObOpenObjectByPointer(proc, OBJ_KERNEL_HANDLE, NULL,
                                   PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                                   *PsProcessType, KernelMode, &hp);
        if (NT_SUCCESS(st)) {
            st = RtlCreateUserThread(hp, NULL, (BOOLEAN)t->CreateSuspended, 0, 0, 0,
                                     (PVOID)t->Start, (PVOID)t->Parameter, &h, NULL);
            ZwClose(hp);
        }
        ObDereferenceObject(proc);
        if (NT_SUCCESS(st) && h) ZwClose(h);
        break;
    }

    case IOCTL_ZULA_MANUAL_MAP_DLL: {
        ZULA_MANUAL_MAP* m;
        if (inLen < sizeof(ZULA_MANUAL_MAP) || outLen < sizeof(ZULA_MANUAL_MAP)) { st = STATUS_BUFFER_TOO_SMALL; break; }
        m = (ZULA_MANUAL_MAP*)buf;
        st = map_dll(m);
        if (NT_SUCCESS(st)) out = sizeof(ZULA_MANUAL_MAP);
        break;
    }

    default:
        st = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

done:
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = out;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return st;
}

static VOID unload(PDRIVER_OBJECT drv)
{
    UNICODE_STRING link;
    RtlInitUnicodeString(&link, ZULA_SYMLINK_NAME);
    IoDeleteSymbolicLink(&link);
    if (g_drv && g_drv->Device) IoDeleteDevice(g_drv->Device);
    if (g_drv) { ExFreePoolWithTag(g_drv, TAG); g_drv = NULL; }
    UNREFERENCED_PARAMETER(drv);
}

/* kdmapper manuel map: DriverEntry NULL DriverObject ile cagrilir
   (kdmapper README: "DriverObject and RegistryPath are NULL"). IoCreateDevice
   ve dispatch kurulumu ayni kalsin diye driver image'i icinde (pool'da,
   kalici) sahte bir DRIVER_OBJECT kullanilir - IoCreateDevice yalnizca
   DriverObject->DeviceObject listesine yazar, ek alan gerekmez. */
static DRIVER_OBJECT g_fakeDrv;
static DRIVER_EXTENSION g_fakeExt;

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg)
{
    UNICODE_STRING dev, link;
    NTSTATUS st;
    ULONG i;

    UNREFERENCED_PARAMETER(reg);

    if (!drv) {
        RtlZeroMemory(&g_fakeDrv, sizeof(g_fakeDrv));
        RtlZeroMemory(&g_fakeExt, sizeof(g_fakeExt));
        g_fakeExt.DriverObject = &g_fakeDrv;
        g_fakeDrv.DriverExtension = &g_fakeExt;
        drv = &g_fakeDrv;
    }

    g_drv = (DRV*)ExAllocatePoolWithTag(NonPagedPool, sizeof(DRV), TAG);
    if (!g_drv) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_drv, sizeof(DRV));

    RtlInitUnicodeString(&dev, ZULA_DEVICE_NAME);
    RtlInitUnicodeString(&link, ZULA_SYMLINK_NAME);

    st = IoCreateDevice(drv, 0, &dev, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_drv->Device);
    if (!NT_SUCCESS(st)) { ExFreePoolWithTag(g_drv, TAG); g_drv = NULL; return st; }

    st = IoCreateSymbolicLink(&link, &dev);
    if (!NT_SUCCESS(st)) {
        IoDeleteDevice(g_drv->Device);
        ExFreePoolWithTag(g_drv, TAG);
        g_drv = NULL;
        return st;
    }

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) drv->MajorFunction[i] = CC;
    drv->DriverUnload = unload;
    g_drv->Device->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}