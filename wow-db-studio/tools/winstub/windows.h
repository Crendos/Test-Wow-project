// Минимальный стаб WinAPI для g++ -fsyntax-only с форсированным #if 1 _WIN32.
// Покрывает только то, что реально используют tools/wow_patch_cli.cpp и
// tools/wow_mem_patcher.cpp. Ссылок нет — проверяется чисто синтаксис.
//
// Использование:
//   sed 's/#ifdef _WIN32/#if 1/' file.cpp > /tmp/x_win.cpp
//   g++ -std=c++20 -Wall -Wextra -fsyntax-only -Itools/winstub -Isrc /tmp/x_win.cpp
#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <cstddef>
#include <cstdint>
#include <cwchar>

using BOOL   = int;
using BYTE   = unsigned char;
using WORD   = unsigned short;
using DWORD  = unsigned int;
using UINT   = unsigned int;
using INT    = int;
using LONG   = int;
using ULONG  = unsigned long;
using LONGLONG     = long long;
using ULONGLONG    = unsigned long long;
using HANDLE       = void*;
using HMODULE      = void*;
using HINSTANCE    = void*;
using LPVOID       = void*;
using LPCVOID      = const void*;
using WCHAR        = wchar_t;
using LPWSTR       = wchar_t*;
using LPCWSTR      = const wchar_t*;
using LPSTR        = char*;
using LPCSTR       = const char*;
using SIZE_T       = std::size_t;
using DWORD_PTR    = std::uintptr_t;
using ULONG_PTR    = std::uintptr_t;
using LONG_PTR     = std::intptr_t;

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#ifndef NULL
#  define NULL 0
#endif
#define IN
#define OUT
#define MAX_PATH 260
#define CP_UTF8 65001u
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(std::intptr_t)-1)

// --- console ---
BOOL SetConsoleOutputCP(UINT codePage);
BOOL SetConsoleCP(UINT codePage);

// --- version ---
struct VS_FIXEDFILEINFO {
    DWORD dwSignature;
    DWORD dwStrucVersion;
    DWORD dwFileVersionMS;
    DWORD dwFileVersionLS;
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
};
DWORD GetFileVersionInfoSizeA(LPCSTR fileName, DWORD* lpdwHandle);
BOOL  GetFileVersionInfoA(LPCSTR fileName, DWORD handle, DWORD len, void* lpData);
BOOL  VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, void** lplpBuffer, UINT* puLen);

// --- process / memory ---
#define PROCESS_VM_READ           0x0010u
#define PROCESS_VM_WRITE          0x0020u
#define PROCESS_VM_OPERATION      0x0008u
#define PROCESS_QUERY_INFORMATION 0x0400u

#define PAGE_NOACCESS           0x01u
#define PAGE_GUARD              0x100u
#define PAGE_EXECUTE_READWRITE  0x40u
#define MEM_COMMIT  0x1000u
#define MEM_PRIVATE 0x20000u
#define MEM_IMAGE   0x1000000u

struct STARTUPINFOW {
    DWORD  cb;
    WORD   wShowWindow;
    DWORD  dwFlags;
};
struct PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
};
struct MEMORY_BASIC_INFORMATION {
    LPVOID  BaseAddress;
    LPVOID  AllocationBase;
    DWORD   AllocationProtect;
    SIZE_T  RegionSize;
    DWORD   State;
    DWORD   Protect;
    DWORD   Type;
};

HANDLE OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
BOOL   ReadProcessMemory(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer,
                         SIZE_T nSize, SIZE_T* lpNumberOfBytesRead);
BOOL   WriteProcessMemory(HANDLE hProcess, LPVOID lpBaseAddress, LPCVOID lpBuffer,
                          SIZE_T nSize, SIZE_T* lpNumberOfBytesWritten);
BOOL   VirtualProtectEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize,
                        DWORD flNewProtect, DWORD* lpflOldProtect);
SIZE_T VirtualQueryEx(HANDLE hProcess, LPCVOID lpAddress,
                      MEMORY_BASIC_INFORMATION* lpBuffer, SIZE_T dwLength);
DWORD  GetLastError(void);
void   Sleep(DWORD dwMilliseconds);
BOOL   CloseHandle(HANDLE hObject);
BOOL   TerminateProcess(HANDLE hProcess, UINT uExitCode);
BOOL   QueryFullProcessImageNameW(HANDLE hProcess, DWORD dwFlags,
                                  LPWSTR lpExeName, DWORD* lpdwSize);

BOOL CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                    void* lpProcessAttributes, void* lpThreadAttributes,
                    BOOL bInheritHandles, DWORD dwCreationFlags,
                    void* lpEnvironment, LPCWSTR lpCurrentDirectory,
                    STARTUPINFOW* lpStartupInfo, PROCESS_INFORMATION* lpProcessInformation);

int WideCharToMultiByte(UINT codePage, DWORD dwFlags, LPCWSTR lpWideCharStr,
                        int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte,
                        LPCSTR lpDefaultChar, BOOL* lpUsedDefaultChar);

// MSVC-специфика; в glibc нет — объявляем для синтаксиса.
int _wcsicmp(const wchar_t* a, const wchar_t* b);

// --- toolhelp ---
#define TH32CS_SNAPPROCESS 0x00000002u
struct PROCESSENTRY32W {
    DWORD   dwSize;
    DWORD   cntUsage;
    DWORD   th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD   th32ModuleID;
    DWORD   cntThreads;
    DWORD   th32ParentProcessID;
    LONG    pcPriClassBase;
    DWORD   dwFlags;
    WCHAR   szExeFile[MAX_PATH];
};
HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL   Process32FirstW(HANDLE hSnapshot, PROCESSENTRY32W* lppe);
BOOL   Process32NextW(HANDLE hSnapshot, PROCESSENTRY32W* lppe);
