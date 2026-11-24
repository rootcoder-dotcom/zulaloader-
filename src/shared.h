#pragma once

#ifdef _KERNEL_MODE
#include <ntifs.h>
#else
#include <Windows.h>
#endif

#define ZULA_DEVICE_NAME     L"\\Device\\zuladrv"
#define ZULA_SYMLINK_NAME    L"\\DosDevices\\zuladrv"
#define ZULA_WIN32_NAME      "\\\\.\\zuladrv"
#define ZULA_SERVICE_NAME    L"zuladrv"
#define ZULA_TARGET          "Zula.exe"
#define ZULA_DLL_PATH        "C:\\zula\\cheat.dll"
#define ZULA_DRIVER_VERSION  0x00020000u
#define ZULA_MAGIC           0x5A554C41u

/* IOCTL kodlari: isimlendirme cakismasini onlemek icin IOCTL_ on eki */
#define IOCTL_ZULA_VERIFY_VERSION  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ZULA_READ_MEMORY     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ZULA_WRITE_MEMORY    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ZULA_GET_MODULE      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ZULA_ALLOC_MEMORY    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ZULA_CREATE_THREAD   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ZULA_MANUAL_MAP_DLL  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 8)

typedef struct _ZULA_VERIFY {
    ULONG Magic;
    ULONG Version;
} ZULA_VERIFY;

typedef struct _ZULA_READ_MEMORY {
    ULONG     Pid;
    ULONG     Pad;
    ULONGLONG Address;
    ULONGLONG Size;
    UCHAR     Data[1];
} ZULA_READ_MEMORY;

typedef struct _ZULA_WRITE_MEMORY {
    ULONG     Pid;
    ULONG     Pad;
    ULONGLONG Address;
    ULONGLONG Size;
    UCHAR     Data[1];
} ZULA_WRITE_MEMORY;

typedef struct _ZULA_GET_MODULE {
    ULONG     Pid;
    ULONG     Pad;
    WCHAR     ModuleName[64];
    ULONGLONG Base;
    ULONGLONG Size;
} ZULA_GET_MODULE;

typedef struct _ZULA_ALLOC_MEMORY {
    ULONG     Pid;
    ULONG     Pad;
    ULONGLONG Size;
    ULONG     Protection;
    ULONG     Pad2;
    ULONGLONG Address;
} ZULA_ALLOC_MEMORY;

typedef struct _ZULA_CREATE_THREAD {
    ULONG     Pid;
    ULONG     Pad;
    ULONGLONG Start;
    ULONGLONG Parameter;
    ULONG     CreateSuspended;
    ULONG     Pad2;
} ZULA_CREATE_THREAD;

typedef struct _ZULA_MANUAL_MAP {
    ULONG     Pid;
    ULONG     Pad;
    ULONGLONG DataVa;
    ULONGLONG DataSize;
    WCHAR     ModuleName[64];
    ULONGLONG Base;
    ULONGLONG Entry;
    ULONGLONG ImageSize;
} ZULA_MANUAL_MAP;

#pragma pack(pop)