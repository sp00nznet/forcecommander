/* Star Wars: Force Commander - import bridge - AUTO-GENERATED, DO NOT EDIT */
/* Regenerate: py -3 gen_imports.py */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "imports.h"

/* 307 imports across 11 DLLs. 248 purge counts derived, 1 hand-written,
 * 58 refused (see gen_imports.py -- a refusal aborts, it does not guess). */
/* ADVAPI32.dll!RegCloseKey  (stdcall, 1 slots, sdk-import-lib) */
static void imp_ADVAPI32__RegCloseKey(void) { IMPORT_STUB("RegCloseKey"); RET(0); STDRET(1); }
/* ADVAPI32.dll!RegCreateKeyExA  (stdcall, 9 slots, sdk-import-lib) */
static void imp_ADVAPI32__RegCreateKeyExA(void) { IMPORT_STUB("RegCreateKeyExA"); RET(0); STDRET(9); }
/* ADVAPI32.dll!RegQueryValueExA  (stdcall, 6 slots, sdk-import-lib) */
static void imp_ADVAPI32__RegQueryValueExA(void) { IMPORT_STUB("RegQueryValueExA"); RET(0); STDRET(6); }
/* ADVAPI32.dll!RegOpenKeyExA  (stdcall, 5 slots, sdk-import-lib) */
static void imp_ADVAPI32__RegOpenKeyExA(void) { IMPORT_STUB("RegOpenKeyExA"); RET(0); STDRET(5); }
/* ADVAPI32.dll!RegSetValueExA  (stdcall, 6 slots, sdk-import-lib) */
static void imp_ADVAPI32__RegSetValueExA(void) { IMPORT_STUB("RegSetValueExA"); RET(0); STDRET(6); }
/* DINPUT.dll!DirectInputCreateA  (stdcall, 4 slots, hand-written) */
static void imp_DINPUT__DirectInputCreateA(void) { IMPORT_STUB("DirectInputCreateA"); RET(0); STDRET(4); }
/* GDI32.dll!GetStockObject  (stdcall, 1 slots, sdk-import-lib) */
static void imp_GDI32__GetStockObject(void) { IMPORT_STUB("GetStockObject"); RET(1); STDRET(1); }
/* GDI32.dll!StretchBlt  (stdcall, 11 slots, sdk-import-lib) */
static void imp_GDI32__StretchBlt(void) { IMPORT_STUB("StretchBlt"); RET(0); STDRET(11); }
/* GDI32.dll!DeleteDC  (stdcall, 1 slots, sdk-import-lib) */
static void imp_GDI32__DeleteDC(void) { IMPORT_STUB("DeleteDC"); RET(0); STDRET(1); }
/* GDI32.dll!SetBkMode  (stdcall, 2 slots, sdk-import-lib) */
static void imp_GDI32__SetBkMode(void) { IMPORT_STUB("SetBkMode"); RET(0); STDRET(2); }
/* GDI32.dll!CreateCompatibleDC  (stdcall, 1 slots, sdk-import-lib) */
static void imp_GDI32__CreateCompatibleDC(void) { IMPORT_STUB("CreateCompatibleDC"); RET(1); STDRET(1); }
/* GDI32.dll!SelectObject  (stdcall, 2 slots, sdk-import-lib) */
static void imp_GDI32__SelectObject(void) { IMPORT_STUB("SelectObject"); RET(1); STDRET(2); }
/* GDI32.dll!SetTextColor  (stdcall, 2 slots, sdk-import-lib) */
static void imp_GDI32__SetTextColor(void) { IMPORT_STUB("SetTextColor"); RET(0); STDRET(2); }
/* GDI32.dll!CreateFontA  (stdcall, 14 slots, sdk-import-lib) */
static void imp_GDI32__CreateFontA(void) { IMPORT_STUB("CreateFontA"); RET(0); STDRET(14); }
/* GDI32.dll!DeleteObject  (stdcall, 1 slots, sdk-import-lib) */
static void imp_GDI32__DeleteObject(void) { IMPORT_STUB("DeleteObject"); RET(0); STDRET(1); }
/* GDI32.dll!TextOutA  (stdcall, 5 slots, sdk-import-lib) */
static void imp_GDI32__TextOutA(void) { IMPORT_STUB("TextOutA"); RET(0); STDRET(5); }
/* GDI32.dll!GetTextExtentPoint32A  (stdcall, 4 slots, sdk-import-lib) */
static void imp_GDI32__GetTextExtentPoint32A(void) { IMPORT_STUB("GetTextExtentPoint32A"); RET(0); STDRET(4); }
/* KERNEL32.dll!Sleep  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__Sleep(void) { IMPORT_STUB("Sleep"); RET(0); STDRET(1); }
/* KERNEL32.dll!FormatMessageA  (stdcall, 7 slots, sdk-import-lib) */
static void imp_KERNEL32__FormatMessageA(void) { IMPORT_STUB("FormatMessageA"); RET(0); STDRET(7); }
/* KERNEL32.dll!CreateFileA  (stdcall, 7 slots, sdk-import-lib) */
static void imp_KERNEL32__CreateFileA(void) { IMPORT_STUB("CreateFileA"); RET(0); STDRET(7); }
/* KERNEL32.dll!GetProcAddress  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__GetProcAddress(void) { IMPORT_STUB("GetProcAddress"); RET(1); STDRET(2); }
/* KERNEL32.dll!FreeLibrary  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__FreeLibrary(void) { IMPORT_STUB("FreeLibrary"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetCurrentThreadId  (stdcall, 0 slots, sdk-import-lib) */
static void imp_KERNEL32__GetCurrentThreadId(void) { IMPORT_STUB("GetCurrentThreadId"); RET(0); STDRET(0); }
/* KERNEL32.dll!lstrlenA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__lstrlenA(void) { IMPORT_STUB("lstrlenA"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetVolumeInformationA  (stdcall, 8 slots, sdk-import-lib) */
static void imp_KERNEL32__GetVolumeInformationA(void) { IMPORT_STUB("GetVolumeInformationA"); RET(0); STDRET(8); }
/* KERNEL32.dll!GetLastError  (stdcall, 0 slots, sdk-import-lib) */
static void imp_KERNEL32__GetLastError(void) { IMPORT_STUB("GetLastError"); RET(0); STDRET(0); }
/* KERNEL32.dll!GetModuleFileNameA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_KERNEL32__GetModuleFileNameA(void) { IMPORT_STUB("GetModuleFileNameA"); RET(1); STDRET(3); }
/* KERNEL32.dll!LocalFree  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__LocalFree(void) { IMPORT_STUB("LocalFree"); RET(0); STDRET(1); }
/* KERNEL32.dll!LoadLibraryA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__LoadLibraryA(void) { IMPORT_STUB("LoadLibraryA"); RET(1); STDRET(1); }
/* KERNEL32.dll!GetVersionExA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__GetVersionExA(void) { IMPORT_STUB("GetVersionExA"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetTickCount  (stdcall, 0 slots, sdk-import-lib) */
static void imp_KERNEL32__GetTickCount(void) { IMPORT_STUB("GetTickCount"); RET(0); STDRET(0); }
/* KERNEL32.dll!CloseHandle  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__CloseHandle(void) { IMPORT_STUB("CloseHandle"); RET(0); STDRET(1); }
/* KERNEL32.dll!CreateMutexA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_KERNEL32__CreateMutexA(void) { IMPORT_STUB("CreateMutexA"); RET(0); STDRET(3); }
/* KERNEL32.dll!QueryPerformanceFrequency  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__QueryPerformanceFrequency(void) { IMPORT_STUB("QueryPerformanceFrequency"); RET(0); STDRET(1); }
/* KERNEL32.dll!QueryPerformanceCounter  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__QueryPerformanceCounter(void) { IMPORT_STUB("QueryPerformanceCounter"); RET(0); STDRET(1); }
/* KERNEL32.dll!CreateEventA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_KERNEL32__CreateEventA(void) { IMPORT_STUB("CreateEventA"); RET(0); STDRET(4); }
/* KERNEL32.dll!GetLocalTime  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__GetLocalTime(void) { IMPORT_STUB("GetLocalTime"); RET(0); STDRET(1); }
/* KERNEL32.dll!Heap32ListNext  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__Heap32ListNext(void) { IMPORT_STUB("Heap32ListNext"); RET(0); STDRET(2); }
/* KERNEL32.dll!Heap32Next  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__Heap32Next(void) { IMPORT_STUB("Heap32Next"); RET(0); STDRET(1); }
/* KERNEL32.dll!Heap32First  (stdcall, 3 slots, sdk-import-lib) */
static void imp_KERNEL32__Heap32First(void) { IMPORT_STUB("Heap32First"); RET(0); STDRET(3); }
/* KERNEL32.dll!Heap32ListFirst  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__Heap32ListFirst(void) { IMPORT_STUB("Heap32ListFirst"); RET(0); STDRET(2); }
/* KERNEL32.dll!Module32Next  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__Module32Next(void) { IMPORT_STUB("Module32Next"); RET(0); STDRET(2); }
/* KERNEL32.dll!CreateToolhelp32Snapshot  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__CreateToolhelp32Snapshot(void) { IMPORT_STUB("CreateToolhelp32Snapshot"); RET(0); STDRET(2); }
/* KERNEL32.dll!GlobalMemoryStatus  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__GlobalMemoryStatus(void) { IMPORT_STUB("GlobalMemoryStatus"); RET(0); STDRET(1); }
/* KERNEL32.dll!Module32First  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__Module32First(void) { IMPORT_STUB("Module32First"); RET(0); STDRET(2); }
/* KERNEL32.dll!WaitForSingleObject  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__WaitForSingleObject(void) { IMPORT_STUB("WaitForSingleObject"); RET(0); STDRET(2); }
/* KERNEL32.dll!SetEvent  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__SetEvent(void) { IMPORT_STUB("SetEvent"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetSystemInfo  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__GetSystemInfo(void) { IMPORT_STUB("GetSystemInfo"); RET(0); STDRET(1); }
/* KERNEL32.dll!ResetEvent  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__ResetEvent(void) { IMPORT_STUB("ResetEvent"); RET(0); STDRET(1); }
/* KERNEL32.dll!ResumeThread  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__ResumeThread(void) { IMPORT_STUB("ResumeThread"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetCurrentThread  (stdcall, 0 slots, sdk-import-lib) */
static void imp_KERNEL32__GetCurrentThread(void) { IMPORT_STUB("GetCurrentThread"); RET(0); STDRET(0); }
/* KERNEL32.dll!LeaveCriticalSection  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__LeaveCriticalSection(void) { IMPORT_STUB("LeaveCriticalSection"); RET(0); STDRET(1); }
/* KERNEL32.dll!EnterCriticalSection  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__EnterCriticalSection(void) { IMPORT_STUB("EnterCriticalSection"); RET(0); STDRET(1); }
/* KERNEL32.dll!CreateThread  (stdcall, 6 slots, sdk-import-lib) */
static void imp_KERNEL32__CreateThread(void) { IMPORT_STUB("CreateThread"); RET(0); STDRET(6); }
/* KERNEL32.dll!DeleteCriticalSection  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__DeleteCriticalSection(void) { IMPORT_STUB("DeleteCriticalSection"); RET(0); STDRET(1); }
/* KERNEL32.dll!ReleaseMutex  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__ReleaseMutex(void) { IMPORT_STUB("ReleaseMutex"); RET(0); STDRET(1); }
/* KERNEL32.dll!InitializeCriticalSection  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__InitializeCriticalSection(void) { IMPORT_STUB("InitializeCriticalSection"); RET(0); STDRET(1); }
/* KERNEL32.dll!SetFilePointer  (stdcall, 4 slots, sdk-import-lib) */
static void imp_KERNEL32__SetFilePointer(void) { IMPORT_STUB("SetFilePointer"); RET(0); STDRET(4); }
/* KERNEL32.dll!ReadFile  (stdcall, 5 slots, sdk-import-lib) */
static void imp_KERNEL32__ReadFile(void) { IMPORT_STUB("ReadFile"); RET(0); STDRET(5); }
/* KERNEL32.dll!PulseEvent  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__PulseEvent(void) { IMPORT_STUB("PulseEvent"); RET(0); STDRET(1); }
/* KERNEL32.dll!SetEndOfFile  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__SetEndOfFile(void) { IMPORT_STUB("SetEndOfFile"); RET(0); STDRET(1); }
/* KERNEL32.dll!DeleteFileA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__DeleteFileA(void) { IMPORT_STUB("DeleteFileA"); RET(0); STDRET(1); }
/* KERNEL32.dll!WriteFile  (stdcall, 5 slots, sdk-import-lib) */
static void imp_KERNEL32__WriteFile(void) { IMPORT_STUB("WriteFile"); RET(0); STDRET(5); }
/* KERNEL32.dll!FindClose  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__FindClose(void) { IMPORT_STUB("FindClose"); RET(0); STDRET(1); }
/* KERNEL32.dll!FindNextFileA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__FindNextFileA(void) { IMPORT_STUB("FindNextFileA"); RET(0); STDRET(2); }
/* KERNEL32.dll!FindFirstFileA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__FindFirstFileA(void) { IMPORT_STUB("FindFirstFileA"); RET(0); STDRET(2); }
/* KERNEL32.dll!CreateDirectoryA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__CreateDirectoryA(void) { IMPORT_STUB("CreateDirectoryA"); RET(0); STDRET(2); }
/* KERNEL32.dll!RemoveDirectoryA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__RemoveDirectoryA(void) { IMPORT_STUB("RemoveDirectoryA"); RET(0); STDRET(1); }
/* KERNEL32.dll!MoveFileA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__MoveFileA(void) { IMPORT_STUB("MoveFileA"); RET(0); STDRET(2); }
/* KERNEL32.dll!SetFileTime  (stdcall, 4 slots, sdk-import-lib) */
static void imp_KERNEL32__SetFileTime(void) { IMPORT_STUB("SetFileTime"); RET(0); STDRET(4); }
/* KERNEL32.dll!SetFileAttributesA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__SetFileAttributesA(void) { IMPORT_STUB("SetFileAttributesA"); RET(0); STDRET(2); }
/* KERNEL32.dll!CopyFileA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_KERNEL32__CopyFileA(void) { IMPORT_STUB("CopyFileA"); RET(0); STDRET(3); }
/* KERNEL32.dll!OutputDebugStringA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__OutputDebugStringA(void) { IMPORT_STUB("OutputDebugStringA"); RET(0); STDRET(1); }
/* KERNEL32.dll!FileTimeToSystemTime  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__FileTimeToSystemTime(void) { IMPORT_STUB("FileTimeToSystemTime"); RET(0); STDRET(2); }
/* KERNEL32.dll!FileTimeToLocalFileTime  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__FileTimeToLocalFileTime(void) { IMPORT_STUB("FileTimeToLocalFileTime"); RET(0); STDRET(2); }
/* KERNEL32.dll!LocalFileTimeToFileTime  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__LocalFileTimeToFileTime(void) { IMPORT_STUB("LocalFileTimeToFileTime"); RET(0); STDRET(2); }
/* KERNEL32.dll!SystemTimeToFileTime  (stdcall, 2 slots, sdk-import-lib) */
static void imp_KERNEL32__SystemTimeToFileTime(void) { IMPORT_STUB("SystemTimeToFileTime"); RET(0); STDRET(2); }
/* KERNEL32.dll!GetModuleHandleA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__GetModuleHandleA(void) { IMPORT_STUB("GetModuleHandleA"); RET(1); STDRET(1); }
/* KERNEL32.dll!GetStartupInfoA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_KERNEL32__GetStartupInfoA(void) { IMPORT_STUB("GetStartupInfoA"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetTempFileNameA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_KERNEL32__GetTempFileNameA(void) { IMPORT_STUB("GetTempFileNameA"); RET(0); STDRET(4); }
/* MSVCP60.dll!??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@ABV01@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Y__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV01_ABV01__Z(void) { IMPORT_REFUSED("MSVCP60.dll!??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@ABV01@@Z", "thiscall"); }
/* MSVCP60.dll!?copy@?$char_traits@D@std@@SAPADPADPBDI@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60___copy___char_traits_D_std__SAPADPADPBDI_Z(void) { IMPORT_STUB("?copy@?$char_traits@D@std@@SAPADPADPBDI@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@0@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_ABV10_0_Z(void) { IMPORT_STUB("??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@0@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??1?$basic_fstream@DU?$char_traits@D@std@@@std@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1__basic_fstream_DU__char_traits_D_std___std__UAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1?$basic_fstream@DU?$char_traits@D@std@@@std@@UAE@XZ", "thiscall"); }
/* MSVCP60.dll!??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Y__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV01_PBD_Z(void) { IMPORT_REFUSED("MSVCP60.dll!??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z", "thiscall"); }
/* MSVCP60.dll!??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0__basic_ios_DU__char_traits_D_std___std__IAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ", "thiscall"); }
/* MSVCP60.dll!?clear@ios_base@std@@QAEXH_N@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___clear_ios_base_std__QAEXH_N_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?clear@ios_base@std@@QAEXH_N@Z", "thiscall"); }
/* MSVCP60.dll!??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____6std__YAAAV__basic_ostream_DU__char_traits_D_std___0_AAV10_PBD_Z(void) { IMPORT_STUB("??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBDABV10@@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_PBDABV10__Z(void) { IMPORT_STUB("??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBDABV10@@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?find_first_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___find_first_not_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?find_first_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z", "thiscall"); }
/* MSVCP60.dll!?max_size@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIXZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___max_size___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIXZ(void) { IMPORT_REFUSED("MSVCP60.dll!?max_size@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIXZ", "thiscall"); }
/* MSVCP60.dll!?find@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___find___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?find@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z", "thiscall"); }
/* MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@D@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_ABV10_D_Z(void) { IMPORT_STUB("??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@D@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??_F?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXXZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60_____F__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEXXZ(void) { IMPORT_REFUSED("MSVCP60.dll!??_F?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXXZ", "thiscall"); }
/* MSVCP60.dll!??9std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____9std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z(void) { IMPORT_STUB("??9std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??Ostd@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Ostd__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z(void) { IMPORT_STUB("??Ostd@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??8std@@YA_NPBDABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____8std__YA_NPBDABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0__Z(void) { IMPORT_STUB("??8std@@YA_NPBDABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?replace@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@IIABV12@II@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___replace___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_IIABV12_II_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?replace@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@IIABV12@II@Z", "thiscall"); }
/* MSVCP60.dll!?find_last_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___find_last_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?find_last_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z", "thiscall"); }
/* MSVCP60.dll!?find_last_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___find_last_not_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?find_last_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z", "thiscall"); }
/* MSVCP60.dll!?copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPADII@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___copy___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPADII_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPADII@Z", "thiscall"); }
/* MSVCP60.dll!?_Copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Copy___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXI_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?_Copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z", "thiscall"); }
/* MSVCP60.dll!?_Xran@std@@YAXXZ  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Xran_std__YAXXZ(void) { IMPORT_STUB("?_Xran@std@@YAXXZ"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?_Split@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Split___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXXZ(void) { IMPORT_REFUSED("MSVCP60.dll!?_Split@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ", "thiscall"); }
/* MSVCP60.dll!?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___setstate___basic_ios_DU__char_traits_D_std___std__QAEXH_N_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z", "thiscall"); }
/* MSVCP60.dll!??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____6std__YAAAV__basic_ostream_DU__char_traits_D_std___0_AAV10_ABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0__Z(void) { IMPORT_STUB("??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??_7?$basic_filebuf@DU?$char_traits@D@std@@@std@@6B@  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60_____7__basic_filebuf_DU__char_traits_D_std___std__6B_(void) { IMPORT_STUB("??_7?$basic_filebuf@DU?$char_traits@D@std@@@std@@6B@"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?close@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___close___basic_filebuf_DU__char_traits_D_std___std__QAEPAV12_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!?close@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@XZ", "thiscall"); }
/* MSVCP60.dll!??1locale@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1locale_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1locale@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1__basic_streambuf_DU__char_traits_D_std___std__UAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAE@XZ", "thiscall"); }
/* MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ID@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___append___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_ID_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ID@Z", "thiscall"); }
/* MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___append___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_PBDI_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z", "thiscall"); }
/* MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___append___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_ABV12_II_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z", "thiscall"); }
/* MSVCP60.dll!??_8?$basic_fstream@DU?$char_traits@D@std@@@std@@7B?$basic_istream@DU?$char_traits@D@std@@@1@@  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60_____8__basic_fstream_DU__char_traits_D_std___std__7B__basic_istream_DU__char_traits_D_std___1__(void) { IMPORT_STUB("??_8?$basic_fstream@DU?$char_traits@D@std@@@std@@7B?$basic_istream@DU?$char_traits@D@std@@@1@@"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?length@?$char_traits@D@std@@SAIPBD@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60___length___char_traits_D_std__SAIPBD_Z(void) { IMPORT_STUB("?length@?$char_traits@D@std@@SAIPBD@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?_Xlen@std@@YAXXZ  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Xlen_std__YAXXZ(void) { IMPORT_STUB("?_Xlen@std@@YAXXZ"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??4?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____4__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV01_PBD_Z(void) { IMPORT_REFUSED("MSVCP60.dll!??4?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z", "thiscall"); }
/* MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBD@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___assign___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_PBD_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBD@Z", "thiscall"); }
/* MSVCP60.dll!??_8?$basic_fstream@DU?$char_traits@D@std@@@std@@7B?$basic_ostream@DU?$char_traits@D@std@@@1@@  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60_____8__basic_fstream_DU__char_traits_D_std___std__7B__basic_ostream_DU__char_traits_D_std___1__(void) { IMPORT_STUB("??_8?$basic_fstream@DU?$char_traits@D@std@@@std@@7B?$basic_ostream@DU?$char_traits@D@std@@@1@@"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??0?$basic_iostream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0__basic_iostream_DU__char_traits_D_std___std__QAE_PAV__basic_streambuf_DU__char_traits_D_std___1__Z(void) { IMPORT_REFUSED("MSVCP60.dll!??0?$basic_iostream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z", "thiscall"); }
/* MSVCP60.dll!??0ios_base@std@@IAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0ios_base_std__IAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??0ios_base@std@@IAE@XZ", "thiscall"); }
/* MSVCP60.dll!??_7?$basic_ios@DU?$char_traits@D@std@@@std@@6B@  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60_____7__basic_ios_DU__char_traits_D_std___std__6B_(void) { IMPORT_STUB("??_7?$basic_ios@DU?$char_traits@D@std@@@std@@6B@"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?open@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@PBDH@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___open___basic_filebuf_DU__char_traits_D_std___std__QAEPAV12_PBDH_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?open@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@PBDH@Z", "thiscall"); }
/* MSVCP60.dll!??0?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAE@PAU_iobuf@@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0__basic_filebuf_DU__char_traits_D_std___std__QAE_PAU_iobuf___Z(void) { IMPORT_REFUSED("MSVCP60.dll!??0?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAE@PAU_iobuf@@@Z", "thiscall"); }
/* MSVCP60.dll!??_7?$basic_fstream@DU?$char_traits@D@std@@@std@@6B@  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60_____7__basic_fstream_DU__char_traits_D_std___std__6B_(void) { IMPORT_STUB("??_7?$basic_fstream@DU?$char_traits@D@std@@@std@@6B@"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??1?$basic_iostream@DU?$char_traits@D@std@@@std@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1__basic_iostream_DU__char_traits_D_std___std__UAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1?$basic_iostream@DU?$char_traits@D@std@@@std@@UAE@XZ", "thiscall"); }
/* MSVCP60.dll!?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___clear___basic_ios_DU__char_traits_D_std___std__QAEXH_N_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z", "thiscall"); }
/* MSVCP60.dll!??1?$basic_filebuf@DU?$char_traits@D@std@@@std@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1__basic_filebuf_DU__char_traits_D_std___std__UAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1?$basic_filebuf@DU?$char_traits@D@std@@@std@@UAE@XZ", "thiscall"); }
/* MSVCP60.dll!??_D?$basic_fstream@DU?$char_traits@D@std@@@std@@QAEXXZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60_____D__basic_fstream_DU__char_traits_D_std___std__QAEXXZ(void) { IMPORT_REFUSED("MSVCP60.dll!??_D?$basic_fstream@DU?$char_traits@D@std@@@std@@QAEXXZ", "thiscall"); }
/* MSVCP60.dll!??1ios_base@std@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1ios_base_std__UAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1ios_base@std@@UAE@XZ", "thiscall"); }
/* MSVCP60.dll!??1?$basic_ios@DU?$char_traits@D@std@@@std@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1__basic_ios_DU__char_traits_D_std___std__UAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1?$basic_ios@DU?$char_traits@D@std@@@std@@UAE@XZ", "thiscall"); }
/* MSVCP60.dll!??1Init@ios_base@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1Init_ios_base_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1Init@ios_base@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!??1_Winit@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1_Winit_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1_Winit@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!??0_Winit@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0_Winit_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??0_Winit@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!?find_first_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___find_first_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?find_first_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z", "thiscall"); }
/* MSVCP60.dll!??0Init@ios_base@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0Init_ios_base_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??0Init@ios_base@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!?substr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBE?AV12@II@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___substr___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBE_AV12_II_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?substr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBE?AV12@II@Z", "thiscall"); }
/* MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@PBD@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_ABV10_PBD_Z(void) { IMPORT_STUB("??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@PBD@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??0_Lockit@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0_Lockit_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??0_Lockit@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!?assign@?$char_traits@D@std@@SAXAADABD@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60___assign___char_traits_D_std__SAXAADABD_Z(void) { IMPORT_STUB("?assign@?$char_traits@D@std@@SAXAADABD@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??Mstd@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____Mstd__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z(void) { IMPORT_STUB("??Mstd@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??1_Lockit@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1_Lockit_std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1_Lockit@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!?npos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@2IB  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60___npos___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__2IB(void) { IMPORT_STUB("?npos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@2IB"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?erase@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@II@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___erase___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_II_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?erase@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@II@Z", "thiscall"); }
/* MSVCP60.dll!?_Grow@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAE_NI_N@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Grow___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAE_NI_N_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?_Grow@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAE_NI_N@Z", "thiscall"); }
/* MSVCP60.dll!?_Eos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Eos___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXI_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?_Eos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z", "thiscall"); }
/* MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___assign___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_ABV12_II_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z", "thiscall"); }
/* MSVCP60.dll!??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____1__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_XZ(void) { IMPORT_REFUSED("MSVCP60.dll!??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ", "thiscall"); }
/* MSVCP60.dll!?_C@?1??_Nullstr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@CAPBDXZ@4DB  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCP60____C__1___Nullstr___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__CAPBDXZ_4DB(void) { IMPORT_STUB("?_C@?1??_Nullstr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@CAPBDXZ@4DB"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?_Tidy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEX_N@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Tidy___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEX_N_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?_Tidy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEX_N@Z", "thiscall"); }
/* MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___assign___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_PBDI_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z", "thiscall"); }
/* MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV?$allocator@D@1@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_ABV__allocator_D_1__Z(void) { IMPORT_REFUSED("MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV?$allocator@D@1@@Z", "thiscall"); }
/* MSVCP60.dll!??9std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____9std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_PBD_Z(void) { IMPORT_STUB("??9std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____8std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_PBD_Z(void) { IMPORT_STUB("??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDABV?$allocator@D@1@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_PBDABV__allocator_D_1__Z(void) { IMPORT_REFUSED("MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDABV?$allocator@D@1@@Z", "thiscall"); }
/* MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____0__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_ABV01__Z(void) { IMPORT_REFUSED("MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z", "thiscall"); }
/* MSVCP60.dll!??A?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEABDI@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____A__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEABDI_Z(void) { IMPORT_REFUSED("MSVCP60.dll!??A?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEABDI@Z", "thiscall"); }
/* MSVCP60.dll!?nothrow@std@@3Unothrow_t@1@B is a DATA import (never called) */
static void imp_MSVCP60___nothrow_std__3Unothrow_t_1_B(void) { IMPORT_REFUSED("MSVCP60.dll!?nothrow@std@@3Unothrow_t@1@B", "data"); }
/* MSVCP60.dll!??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCP60____8std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z(void) { IMPORT_STUB("??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z"); RET(0); CDECLRET(); }
/* MSVCP60.dll!?_Refcnt@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEAAEPBD@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Refcnt___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEAAEPBD_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?_Refcnt@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEAAEPBD@Z", "thiscall"); }
/* MSVCP60.dll!?_Freeze@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60____Freeze___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXXZ(void) { IMPORT_REFUSED("MSVCP60.dll!?_Freeze@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ", "thiscall"); }
/* MSVCP60.dll!?resize@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXI@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCP60___resize___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEXI_Z(void) { IMPORT_REFUSED("MSVCP60.dll!?resize@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXI@Z", "thiscall"); }
/* MSVCRT.dll!?terminate@@YAXXZ  (cdecl, 0 slots, mangled-convention) */
static void imp_MSVCRT___terminate__YAXXZ(void) { IMPORT_STUB("?terminate@@YAXXZ"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_onexit  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___onexit(void) { IMPORT_STUB("_onexit"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__dllonexit  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____dllonexit(void) { IMPORT_STUB("__dllonexit"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_except_handler3  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___except_handler3(void) { IMPORT_STUB("_except_handler3"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__set_app_type  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____set_app_type(void) { IMPORT_STUB("__set_app_type"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__p__fmode  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____p__fmode(void) { IMPORT_STUB("__p__fmode"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__p__commode  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____p__commode(void) { IMPORT_STUB("__p__commode"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_adjust_fdiv  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___adjust_fdiv(void) { IMPORT_STUB("_adjust_fdiv"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__setusermatherr  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____setusermatherr(void) { IMPORT_STUB("__setusermatherr"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_initterm  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___initterm(void) { IMPORT_STUB("_initterm"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__getmainargs  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____getmainargs(void) { IMPORT_STUB("__getmainargs"); RET(0); CDECLRET(); }
/* MSVCRT.dll!exit  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__exit(void) { IMPORT_STUB("exit"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_XcptFilter  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___XcptFilter(void) { IMPORT_STUB("_XcptFilter"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_exit  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___exit(void) { IMPORT_STUB("_exit"); RET(0); CDECLRET(); }
/* MSVCRT.dll!??1type_info@@UAE@XZ  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCRT____1type_info__UAE_XZ(void) { IMPORT_REFUSED("MSVCRT.dll!??1type_info@@UAE@XZ", "thiscall"); }
/* MSVCRT.dll!fgets  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__fgets(void) { IMPORT_STUB("fgets"); RET(0); CDECLRET(); }
/* MSVCRT.dll!fseek  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__fseek(void) { IMPORT_STUB("fseek"); RET(0); CDECLRET(); }
/* MSVCRT.dll!ftell  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__ftell(void) { IMPORT_STUB("ftell"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isalpha  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isalpha(void) { IMPORT_STUB("isalpha"); RET(0); CDECLRET(); }
/* MSVCRT.dll!strncmp  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__strncmp(void) { IMPORT_STUB("strncmp"); RET(0); CDECLRET(); }
/* MSVCRT.dll!mktime  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__mktime(void) { IMPORT_STUB("mktime"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_controlfp  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___controlfp(void) { IMPORT_STUB("_controlfp"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_splitpath  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___splitpath(void) { IMPORT_STUB("_splitpath"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_fullpath  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___fullpath(void) { IMPORT_STUB("_fullpath"); RET(0); CDECLRET(); }
/* MSVCRT.dll!memchr  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__memchr(void) { IMPORT_STUB("memchr"); RET(0); CDECLRET(); }
/* MSVCRT.dll!atoi  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__atoi(void) { IMPORT_STUB("atoi"); RET(0); CDECLRET(); }
/* MSVCRT.dll!strncpy  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__strncpy(void) { IMPORT_STUB("strncpy"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_stricmp  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___stricmp(void) { IMPORT_STUB("_stricmp"); RET(0); CDECLRET(); }
/* MSVCRT.dll!vsprintf  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__vsprintf(void) { IMPORT_STUB("vsprintf"); RET(0); CDECLRET(); }
/* MSVCRT.dll!fprintf  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__fprintf(void) { IMPORT_STUB("fprintf"); RET(0); CDECLRET(); }
/* MSVCRT.dll!fopen  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__fopen(void) { IMPORT_STUB("fopen"); RET(0); CDECLRET(); }
/* MSVCRT.dll!fread  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__fread(void) { IMPORT_STUB("fread"); RET(0); CDECLRET(); }
/* MSVCRT.dll!fclose  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__fclose(void) { IMPORT_STUB("fclose"); RET(0); CDECLRET(); }
/* MSVCRT.dll!ceil  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__ceil(void) { IMPORT_STUB("ceil"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_purecall  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___purecall(void) { IMPORT_STUB("_purecall"); RET(0); CDECLRET(); }
/* MSVCRT.dll!sscanf  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__sscanf(void) { IMPORT_STUB("sscanf"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_CIacos  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___CIacos(void) { IMPORT_STUB("_CIacos"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_CIasin  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___CIasin(void) { IMPORT_STUB("_CIasin"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__RTDynamicCast  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____RTDynamicCast(void) { IMPORT_STUB("__RTDynamicCast"); RET(0); CDECLRET(); }
/* MSVCRT.dll!memmove  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__memmove(void) { IMPORT_STUB("memmove"); RET(0); CDECLRET(); }
/* MSVCRT.dll!sprintf  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__sprintf(void) { IMPORT_STUB("sprintf"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_ftol  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___ftol(void) { IMPORT_STUB("_ftol"); RET(0); CDECLRET(); }
/* MSVCRT.dll!??2@YAPAXI@Z  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____2_YAPAXI_Z(void) { IMPORT_STUB("??2@YAPAXI@Z"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__CxxFrameHandler  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____CxxFrameHandler(void) { IMPORT_STUB("__CxxFrameHandler"); RET(0); CDECLRET(); }
/* MSVCRT.dll!free  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__free(void) { IMPORT_STUB("free"); RET(0); CDECLRET(); }
/* MSVCRT.dll!malloc  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__malloc(void) { IMPORT_STUB("malloc"); RET(1); CDECLRET(); }
/* MSVCRT.dll!_CxxThrowException  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___CxxThrowException(void) { IMPORT_STUB("_CxxThrowException"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_acmdln  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___acmdln(void) { IMPORT_STUB("_acmdln"); RET(0); CDECLRET(); }
/* MSVCRT.dll!atof  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__atof(void) { IMPORT_STUB("atof"); RET(0); CDECLRET(); }
/* MSVCRT.dll!bsearch  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__bsearch(void) { IMPORT_STUB("bsearch"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_strnicmp  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___strnicmp(void) { IMPORT_STUB("_strnicmp"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_CIfmod  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___CIfmod(void) { IMPORT_STUB("_CIfmod"); RET(0); CDECLRET(); }
/* MSVCRT.dll!??8type_info@@QBEHABV0@@Z  thiscall -- purge count NOT derivable, see gen_imports.py */
static void imp_MSVCRT____8type_info__QBEHABV0__Z(void) { IMPORT_REFUSED("MSVCRT.dll!??8type_info@@QBEHABV0@@Z", "thiscall"); }
/* MSVCRT.dll!floor  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__floor(void) { IMPORT_STUB("floor"); RET(0); CDECLRET(); }
/* MSVCRT.dll!toupper  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__toupper(void) { IMPORT_STUB("toupper"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_CIpow  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___CIpow(void) { IMPORT_STUB("_CIpow"); RET(0); CDECLRET(); }
/* MSVCRT.dll!__RTtypeid  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT____RTtypeid(void) { IMPORT_STUB("__RTtypeid"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isdigit  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isdigit(void) { IMPORT_STUB("isdigit"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isxdigit  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isxdigit(void) { IMPORT_STUB("isxdigit"); RET(0); CDECLRET(); }
/* MSVCRT.dll!tolower  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__tolower(void) { IMPORT_STUB("tolower"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isupper  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isupper(void) { IMPORT_STUB("isupper"); RET(0); CDECLRET(); }
/* MSVCRT.dll!islower  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__islower(void) { IMPORT_STUB("islower"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isalnum  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isalnum(void) { IMPORT_STUB("isalnum"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isspace  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isspace(void) { IMPORT_STUB("isspace"); RET(0); CDECLRET(); }
/* MSVCRT.dll!_callnewh  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT___callnewh(void) { IMPORT_STUB("_callnewh"); RET(0); CDECLRET(); }
/* MSVCRT.dll!isprint  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__isprint(void) { IMPORT_STUB("isprint"); RET(0); CDECLRET(); }
/* MSVCRT.dll!realloc  (cdecl, 0 slots, crt-undecorated-export) */
static void imp_MSVCRT__realloc(void) { IMPORT_STUB("realloc"); RET(0); CDECLRET(); }
/* SHELL32.dll!ShellExecuteA  (stdcall, 6 slots, sdk-import-lib) */
static void imp_SHELL32__ShellExecuteA(void) { IMPORT_STUB("ShellExecuteA"); RET(0); STDRET(6); }
/* USER32.dll!CreateWindowExA  (stdcall, 12 slots, sdk-import-lib) */
static void imp_USER32__CreateWindowExA(void) { IMPORT_STUB("CreateWindowExA"); RET(1); STDRET(12); }
/* USER32.dll!CharPrevA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__CharPrevA(void) { IMPORT_STUB("CharPrevA"); RET(0); STDRET(2); }
/* USER32.dll!PostThreadMessageA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_USER32__PostThreadMessageA(void) { IMPORT_STUB("PostThreadMessageA"); RET(0); STDRET(4); }
/* USER32.dll!SetWindowTextA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__SetWindowTextA(void) { IMPORT_STUB("SetWindowTextA"); RET(0); STDRET(2); }
/* USER32.dll!SetWindowPos  (stdcall, 7 slots, sdk-import-lib) */
static void imp_USER32__SetWindowPos(void) { IMPORT_STUB("SetWindowPos"); RET(0); STDRET(7); }
/* USER32.dll!GetSystemMetrics  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__GetSystemMetrics(void) { IMPORT_STUB("GetSystemMetrics"); RET(0); STDRET(1); }
/* USER32.dll!EndPaint  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__EndPaint(void) { IMPORT_STUB("EndPaint"); RET(0); STDRET(2); }
/* USER32.dll!CreateDialogParamA  (stdcall, 5 slots, sdk-import-lib) */
static void imp_USER32__CreateDialogParamA(void) { IMPORT_STUB("CreateDialogParamA"); RET(0); STDRET(5); }
/* USER32.dll!SetCursor  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__SetCursor(void) { IMPORT_STUB("SetCursor"); RET(0); STDRET(1); }
/* USER32.dll!PeekMessageA  (stdcall, 5 slots, sdk-import-lib) */
static void imp_USER32__PeekMessageA(void) { IMPORT_STUB("PeekMessageA"); RET(0); STDRET(5); }
/* USER32.dll!GetWindowTextA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_USER32__GetWindowTextA(void) { IMPORT_STUB("GetWindowTextA"); RET(0); STDRET(3); }
/* USER32.dll!MsgWaitForMultipleObjects  (stdcall, 5 slots, sdk-import-lib) */
static void imp_USER32__MsgWaitForMultipleObjects(void) { IMPORT_STUB("MsgWaitForMultipleObjects"); RET(0); STDRET(5); }
/* USER32.dll!DispatchMessageA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__DispatchMessageA(void) { IMPORT_STUB("DispatchMessageA"); RET(0); STDRET(1); }
/* USER32.dll!GetMessageA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_USER32__GetMessageA(void) { IMPORT_STUB("GetMessageA"); RET(0); STDRET(4); }
/* USER32.dll!TranslateMessage  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__TranslateMessage(void) { IMPORT_STUB("TranslateMessage"); RET(0); STDRET(1); }
/* USER32.dll!RegisterClassExA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__RegisterClassExA(void) { IMPORT_STUB("RegisterClassExA"); RET(0); STDRET(1); }
/* USER32.dll!CharNextA  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__CharNextA(void) { IMPORT_STUB("CharNextA"); RET(0); STDRET(1); }
/* USER32.dll!ShowWindow  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__ShowWindow(void) { IMPORT_STUB("ShowWindow"); RET(1); STDRET(2); }
/* USER32.dll!GetWindowRect  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__GetWindowRect(void) { IMPORT_STUB("GetWindowRect"); RET(0); STDRET(2); }
/* USER32.dll!BeginPaint  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__BeginPaint(void) { IMPORT_STUB("BeginPaint"); RET(0); STDRET(2); }
/* USER32.dll!GetUpdateRect  (stdcall, 3 slots, sdk-import-lib) */
static void imp_USER32__GetUpdateRect(void) { IMPORT_STUB("GetUpdateRect"); RET(0); STDRET(3); }
/* USER32.dll!LoadBitmapA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__LoadBitmapA(void) { IMPORT_STUB("LoadBitmapA"); RET(0); STDRET(2); }
/* USER32.dll!LoadCursorA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__LoadCursorA(void) { IMPORT_STUB("LoadCursorA"); RET(1); STDRET(2); }
/* USER32.dll!MessageBoxA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_USER32__MessageBoxA(void) { IMPORT_STUB("MessageBoxA"); RET(0); STDRET(4); }
/* USER32.dll!UpdateWindow  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__UpdateWindow(void) { IMPORT_STUB("UpdateWindow"); RET(0); STDRET(1); }
/* USER32.dll!IsWindowVisible  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__IsWindowVisible(void) { IMPORT_STUB("IsWindowVisible"); RET(0); STDRET(1); }
/* USER32.dll!SetParent  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__SetParent(void) { IMPORT_STUB("SetParent"); RET(0); STDRET(2); }
/* USER32.dll!DestroyWindow  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__DestroyWindow(void) { IMPORT_STUB("DestroyWindow"); RET(0); STDRET(1); }
/* USER32.dll!IsWindow  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__IsWindow(void) { IMPORT_STUB("IsWindow"); RET(0); STDRET(1); }
/* USER32.dll!IsIconic  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__IsIconic(void) { IMPORT_STUB("IsIconic"); RET(0); STDRET(1); }
/* USER32.dll!GetDesktopWindow  (stdcall, 0 slots, sdk-import-lib) */
static void imp_USER32__GetDesktopWindow(void) { IMPORT_STUB("GetDesktopWindow"); RET(0); STDRET(0); }
/* USER32.dll!ClientToScreen  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__ClientToScreen(void) { IMPORT_STUB("ClientToScreen"); RET(0); STDRET(2); }
/* USER32.dll!SetCursorPos  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__SetCursorPos(void) { IMPORT_STUB("SetCursorPos"); RET(0); STDRET(2); }
/* USER32.dll!GetKeyState  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__GetKeyState(void) { IMPORT_STUB("GetKeyState"); RET(0); STDRET(1); }
/* USER32.dll!ShowCursor  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__ShowCursor(void) { IMPORT_STUB("ShowCursor"); RET(0); STDRET(1); }
/* USER32.dll!GetParent  (stdcall, 1 slots, sdk-import-lib) */
static void imp_USER32__GetParent(void) { IMPORT_STUB("GetParent"); RET(0); STDRET(1); }
/* USER32.dll!SetWindowPlacement  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__SetWindowPlacement(void) { IMPORT_STUB("SetWindowPlacement"); RET(0); STDRET(2); }
/* USER32.dll!GetWindowLongA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__GetWindowLongA(void) { IMPORT_STUB("GetWindowLongA"); RET(0); STDRET(2); }
/* USER32.dll!GetWindowPlacement  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__GetWindowPlacement(void) { IMPORT_STUB("GetWindowPlacement"); RET(0); STDRET(2); }
/* USER32.dll!SetWindowLongA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_USER32__SetWindowLongA(void) { IMPORT_STUB("SetWindowLongA"); RET(0); STDRET(3); }
/* USER32.dll!GetClientRect  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__GetClientRect(void) { IMPORT_STUB("GetClientRect"); RET(0); STDRET(2); }
/* USER32.dll!DefWindowProcA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_USER32__DefWindowProcA(void) { IMPORT_STUB("DefWindowProcA"); RET(0); STDRET(4); }
/* USER32.dll!SetPropA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_USER32__SetPropA(void) { IMPORT_STUB("SetPropA"); RET(0); STDRET(3); }
/* USER32.dll!GetPropA  (stdcall, 2 slots, sdk-import-lib) */
static void imp_USER32__GetPropA(void) { IMPORT_STUB("GetPropA"); RET(0); STDRET(2); }
/* WINMM.dll!timeSetEvent  (stdcall, 5 slots, sdk-import-lib) */
static void imp_WINMM__timeSetEvent(void) { IMPORT_STUB("timeSetEvent"); RET(0); STDRET(5); }
/* WINMM.dll!timeEndPeriod  (stdcall, 1 slots, sdk-import-lib) */
static void imp_WINMM__timeEndPeriod(void) { IMPORT_STUB("timeEndPeriod"); RET(0); STDRET(1); }
/* WINMM.dll!timeGetTime  (stdcall, 0 slots, sdk-import-lib) */
static void imp_WINMM__timeGetTime(void) { IMPORT_STUB("timeGetTime"); RET(0); STDRET(0); }
/* WINMM.dll!mciSendStringA  (stdcall, 4 slots, sdk-import-lib) */
static void imp_WINMM__mciSendStringA(void) { IMPORT_STUB("mciSendStringA"); RET(0); STDRET(4); }
/* WINMM.dll!PlaySoundA  (stdcall, 3 slots, sdk-import-lib) */
static void imp_WINMM__PlaySoundA(void) { IMPORT_STUB("PlaySoundA"); RET(0); STDRET(3); }
/* WINMM.dll!timeKillEvent  (stdcall, 1 slots, sdk-import-lib) */
static void imp_WINMM__timeKillEvent(void) { IMPORT_STUB("timeKillEvent"); RET(0); STDRET(1); }
/* WINMM.dll!timeBeginPeriod  (stdcall, 1 slots, sdk-import-lib) */
static void imp_WINMM__timeBeginPeriod(void) { IMPORT_STUB("timeBeginPeriod"); RET(0); STDRET(1); }
/* WINMM.dll!timeGetDevCaps  (stdcall, 2 slots, sdk-import-lib) */
static void imp_WINMM__timeGetDevCaps(void) { IMPORT_STUB("timeGetDevCaps"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_startup@0  (stdcall, 0 slots, decorated-name) */
static void imp_mss32___AIL_startup_0(void) { IMPORT_STUB("_AIL_startup@0"); RET(0); STDRET(0); }
/* mss32.dll!_AIL_digital_handle_release@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_digital_handle_release_4(void) { IMPORT_STUB("_AIL_digital_handle_release@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_digital_handle_reacquire@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_digital_handle_reacquire_4(void) { IMPORT_STUB("_AIL_digital_handle_reacquire@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_allocate_sample_handle@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_allocate_sample_handle_4(void) { IMPORT_STUB("_AIL_allocate_sample_handle@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_set_DirectSound_HWND@8  (stdcall, 2 slots, decorated-name) */
static void imp_mss32___AIL_set_DirectSound_HWND_8(void) { IMPORT_STUB("_AIL_set_DirectSound_HWND@8"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_last_error@0  (stdcall, 0 slots, decorated-name) */
static void imp_mss32___AIL_last_error_0(void) { IMPORT_STUB("_AIL_last_error@0"); RET(0); STDRET(0); }
/* mss32.dll!_AIL_waveOutOpen@16  (stdcall, 4 slots, decorated-name) */
static void imp_mss32___AIL_waveOutOpen_16(void) { IMPORT_STUB("_AIL_waveOutOpen@16"); RET(0); STDRET(4); }
/* mss32.dll!_AIL_set_preference@8  (stdcall, 2 slots, decorated-name) */
static void imp_mss32___AIL_set_preference_8(void) { IMPORT_STUB("_AIL_set_preference@8"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_set_sample_pan@8  (stdcall, 2 slots, decorated-name) */
static void imp_mss32___AIL_set_sample_pan_8(void) { IMPORT_STUB("_AIL_set_sample_pan@8"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_shutdown@0  (stdcall, 0 slots, decorated-name) */
static void imp_mss32___AIL_shutdown_0(void) { IMPORT_STUB("_AIL_shutdown@0"); RET(0); STDRET(0); }
/* mss32.dll!_AIL_waveOutClose@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_waveOutClose_4(void) { IMPORT_STUB("_AIL_waveOutClose@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_release_sample_handle@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_release_sample_handle_4(void) { IMPORT_STUB("_AIL_release_sample_handle@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_get_DirectSound_info@12  (stdcall, 3 slots, decorated-name) */
static void imp_mss32___AIL_get_DirectSound_info_12(void) { IMPORT_STUB("_AIL_get_DirectSound_info@12"); RET(0); STDRET(3); }
/* mss32.dll!_AIL_set_sample_playback_rate@8  (stdcall, 2 slots, decorated-name) */
static void imp_mss32___AIL_set_sample_playback_rate_8(void) { IMPORT_STUB("_AIL_set_sample_playback_rate@8"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_set_named_sample_file@20  (stdcall, 5 slots, decorated-name) */
static void imp_mss32___AIL_set_named_sample_file_20(void) { IMPORT_STUB("_AIL_set_named_sample_file@20"); RET(0); STDRET(5); }
/* mss32.dll!_AIL_start_sample@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_start_sample_4(void) { IMPORT_STUB("_AIL_start_sample@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_set_sample_loop_count@8  (stdcall, 2 slots, decorated-name) */
static void imp_mss32___AIL_set_sample_loop_count_8(void) { IMPORT_STUB("_AIL_set_sample_loop_count@8"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_set_sample_file@12  (stdcall, 3 slots, decorated-name) */
static void imp_mss32___AIL_set_sample_file_12(void) { IMPORT_STUB("_AIL_set_sample_file@12"); RET(0); STDRET(3); }
/* mss32.dll!_AIL_set_sample_volume@8  (stdcall, 2 slots, decorated-name) */
static void imp_mss32___AIL_set_sample_volume_8(void) { IMPORT_STUB("_AIL_set_sample_volume@8"); RET(0); STDRET(2); }
/* mss32.dll!_AIL_end_sample@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_end_sample_4(void) { IMPORT_STUB("_AIL_end_sample@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_init_sample@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_init_sample_4(void) { IMPORT_STUB("_AIL_init_sample@4"); RET(0); STDRET(1); }
/* mss32.dll!_AIL_sample_status@4  (stdcall, 1 slots, decorated-name) */
static void imp_mss32___AIL_sample_status_4(void) { IMPORT_STUB("_AIL_sample_status@4"); RET(0); STDRET(1); }
/* ole32.dll!CoInitialize  (stdcall, 1 slots, sdk-import-lib) */
static void imp_ole32__CoInitialize(void) { IMPORT_STUB("CoInitialize"); RET(1); STDRET(1); }
/* ole32.dll!CoUninitialize  (stdcall, 0 slots, sdk-import-lib) */
static void imp_ole32__CoUninitialize(void) { IMPORT_STUB("CoUninitialize"); RET(0); STDRET(0); }
/* ole32.dll!CoCreateInstance  (stdcall, 5 slots, sdk-import-lib) */
static void imp_ole32__CoCreateInstance(void) { IMPORT_STUB("CoCreateInstance"); RET(0); STDRET(5); }

const import_entry_t g_imports[] = {
    { 0x007C3000u, imp_ADVAPI32__RegCloseKey, "ADVAPI32.dll!RegCloseKey" },
    { 0x007C3004u, imp_ADVAPI32__RegCreateKeyExA, "ADVAPI32.dll!RegCreateKeyExA" },
    { 0x007C3008u, imp_ADVAPI32__RegQueryValueExA, "ADVAPI32.dll!RegQueryValueExA" },
    { 0x007C300Cu, imp_ADVAPI32__RegOpenKeyExA, "ADVAPI32.dll!RegOpenKeyExA" },
    { 0x007C3010u, imp_ADVAPI32__RegSetValueExA, "ADVAPI32.dll!RegSetValueExA" },
    { 0x007C3018u, imp_DINPUT__DirectInputCreateA, "DINPUT.dll!DirectInputCreateA" },
    { 0x007C3020u, imp_GDI32__GetStockObject, "GDI32.dll!GetStockObject" },
    { 0x007C3024u, imp_GDI32__StretchBlt, "GDI32.dll!StretchBlt" },
    { 0x007C3028u, imp_GDI32__DeleteDC, "GDI32.dll!DeleteDC" },
    { 0x007C302Cu, imp_GDI32__SetBkMode, "GDI32.dll!SetBkMode" },
    { 0x007C3030u, imp_GDI32__CreateCompatibleDC, "GDI32.dll!CreateCompatibleDC" },
    { 0x007C3034u, imp_GDI32__SelectObject, "GDI32.dll!SelectObject" },
    { 0x007C3038u, imp_GDI32__SetTextColor, "GDI32.dll!SetTextColor" },
    { 0x007C303Cu, imp_GDI32__CreateFontA, "GDI32.dll!CreateFontA" },
    { 0x007C3040u, imp_GDI32__DeleteObject, "GDI32.dll!DeleteObject" },
    { 0x007C3044u, imp_GDI32__TextOutA, "GDI32.dll!TextOutA" },
    { 0x007C3048u, imp_GDI32__GetTextExtentPoint32A, "GDI32.dll!GetTextExtentPoint32A" },
    { 0x007C3050u, imp_KERNEL32__Sleep, "KERNEL32.dll!Sleep" },
    { 0x007C3054u, imp_KERNEL32__FormatMessageA, "KERNEL32.dll!FormatMessageA" },
    { 0x007C3058u, imp_KERNEL32__CreateFileA, "KERNEL32.dll!CreateFileA" },
    { 0x007C305Cu, imp_KERNEL32__GetProcAddress, "KERNEL32.dll!GetProcAddress" },
    { 0x007C3060u, imp_KERNEL32__FreeLibrary, "KERNEL32.dll!FreeLibrary" },
    { 0x007C3064u, imp_KERNEL32__GetCurrentThreadId, "KERNEL32.dll!GetCurrentThreadId" },
    { 0x007C3068u, imp_KERNEL32__lstrlenA, "KERNEL32.dll!lstrlenA" },
    { 0x007C306Cu, imp_KERNEL32__GetVolumeInformationA, "KERNEL32.dll!GetVolumeInformationA" },
    { 0x007C3070u, imp_KERNEL32__GetLastError, "KERNEL32.dll!GetLastError" },
    { 0x007C3074u, imp_KERNEL32__GetModuleFileNameA, "KERNEL32.dll!GetModuleFileNameA" },
    { 0x007C3078u, imp_KERNEL32__LocalFree, "KERNEL32.dll!LocalFree" },
    { 0x007C307Cu, imp_KERNEL32__LoadLibraryA, "KERNEL32.dll!LoadLibraryA" },
    { 0x007C3080u, imp_KERNEL32__GetVersionExA, "KERNEL32.dll!GetVersionExA" },
    { 0x007C3084u, imp_KERNEL32__GetTickCount, "KERNEL32.dll!GetTickCount" },
    { 0x007C3088u, imp_KERNEL32__CloseHandle, "KERNEL32.dll!CloseHandle" },
    { 0x007C308Cu, imp_KERNEL32__CreateMutexA, "KERNEL32.dll!CreateMutexA" },
    { 0x007C3090u, imp_KERNEL32__QueryPerformanceFrequency, "KERNEL32.dll!QueryPerformanceFrequency" },
    { 0x007C3094u, imp_KERNEL32__QueryPerformanceCounter, "KERNEL32.dll!QueryPerformanceCounter" },
    { 0x007C3098u, imp_KERNEL32__CreateEventA, "KERNEL32.dll!CreateEventA" },
    { 0x007C309Cu, imp_KERNEL32__GetLocalTime, "KERNEL32.dll!GetLocalTime" },
    { 0x007C30A0u, imp_KERNEL32__Heap32ListNext, "KERNEL32.dll!Heap32ListNext" },
    { 0x007C30A4u, imp_KERNEL32__Heap32Next, "KERNEL32.dll!Heap32Next" },
    { 0x007C30A8u, imp_KERNEL32__Heap32First, "KERNEL32.dll!Heap32First" },
    { 0x007C30ACu, imp_KERNEL32__Heap32ListFirst, "KERNEL32.dll!Heap32ListFirst" },
    { 0x007C30B0u, imp_KERNEL32__Module32Next, "KERNEL32.dll!Module32Next" },
    { 0x007C30B4u, imp_KERNEL32__CreateToolhelp32Snapshot, "KERNEL32.dll!CreateToolhelp32Snapshot" },
    { 0x007C30B8u, imp_KERNEL32__GlobalMemoryStatus, "KERNEL32.dll!GlobalMemoryStatus" },
    { 0x007C30BCu, imp_KERNEL32__Module32First, "KERNEL32.dll!Module32First" },
    { 0x007C30C0u, imp_KERNEL32__WaitForSingleObject, "KERNEL32.dll!WaitForSingleObject" },
    { 0x007C30C4u, imp_KERNEL32__SetEvent, "KERNEL32.dll!SetEvent" },
    { 0x007C30C8u, imp_KERNEL32__GetSystemInfo, "KERNEL32.dll!GetSystemInfo" },
    { 0x007C30CCu, imp_KERNEL32__ResetEvent, "KERNEL32.dll!ResetEvent" },
    { 0x007C30D0u, imp_KERNEL32__ResumeThread, "KERNEL32.dll!ResumeThread" },
    { 0x007C30D4u, imp_KERNEL32__GetCurrentThread, "KERNEL32.dll!GetCurrentThread" },
    { 0x007C30D8u, imp_KERNEL32__LeaveCriticalSection, "KERNEL32.dll!LeaveCriticalSection" },
    { 0x007C30DCu, imp_KERNEL32__EnterCriticalSection, "KERNEL32.dll!EnterCriticalSection" },
    { 0x007C30E0u, imp_KERNEL32__CreateThread, "KERNEL32.dll!CreateThread" },
    { 0x007C30E4u, imp_KERNEL32__DeleteCriticalSection, "KERNEL32.dll!DeleteCriticalSection" },
    { 0x007C30E8u, imp_KERNEL32__ReleaseMutex, "KERNEL32.dll!ReleaseMutex" },
    { 0x007C30ECu, imp_KERNEL32__InitializeCriticalSection, "KERNEL32.dll!InitializeCriticalSection" },
    { 0x007C30F0u, imp_KERNEL32__SetFilePointer, "KERNEL32.dll!SetFilePointer" },
    { 0x007C30F4u, imp_KERNEL32__ReadFile, "KERNEL32.dll!ReadFile" },
    { 0x007C30F8u, imp_KERNEL32__PulseEvent, "KERNEL32.dll!PulseEvent" },
    { 0x007C30FCu, imp_KERNEL32__SetEndOfFile, "KERNEL32.dll!SetEndOfFile" },
    { 0x007C3100u, imp_KERNEL32__DeleteFileA, "KERNEL32.dll!DeleteFileA" },
    { 0x007C3104u, imp_KERNEL32__WriteFile, "KERNEL32.dll!WriteFile" },
    { 0x007C3108u, imp_KERNEL32__FindClose, "KERNEL32.dll!FindClose" },
    { 0x007C310Cu, imp_KERNEL32__FindNextFileA, "KERNEL32.dll!FindNextFileA" },
    { 0x007C3110u, imp_KERNEL32__FindFirstFileA, "KERNEL32.dll!FindFirstFileA" },
    { 0x007C3114u, imp_KERNEL32__CreateDirectoryA, "KERNEL32.dll!CreateDirectoryA" },
    { 0x007C3118u, imp_KERNEL32__RemoveDirectoryA, "KERNEL32.dll!RemoveDirectoryA" },
    { 0x007C311Cu, imp_KERNEL32__MoveFileA, "KERNEL32.dll!MoveFileA" },
    { 0x007C3120u, imp_KERNEL32__SetFileTime, "KERNEL32.dll!SetFileTime" },
    { 0x007C3124u, imp_KERNEL32__SetFileAttributesA, "KERNEL32.dll!SetFileAttributesA" },
    { 0x007C3128u, imp_KERNEL32__CopyFileA, "KERNEL32.dll!CopyFileA" },
    { 0x007C312Cu, imp_KERNEL32__OutputDebugStringA, "KERNEL32.dll!OutputDebugStringA" },
    { 0x007C3130u, imp_KERNEL32__FileTimeToSystemTime, "KERNEL32.dll!FileTimeToSystemTime" },
    { 0x007C3134u, imp_KERNEL32__FileTimeToLocalFileTime, "KERNEL32.dll!FileTimeToLocalFileTime" },
    { 0x007C3138u, imp_KERNEL32__LocalFileTimeToFileTime, "KERNEL32.dll!LocalFileTimeToFileTime" },
    { 0x007C313Cu, imp_KERNEL32__SystemTimeToFileTime, "KERNEL32.dll!SystemTimeToFileTime" },
    { 0x007C3140u, imp_KERNEL32__GetModuleHandleA, "KERNEL32.dll!GetModuleHandleA" },
    { 0x007C3144u, imp_KERNEL32__GetStartupInfoA, "KERNEL32.dll!GetStartupInfoA" },
    { 0x007C3148u, imp_KERNEL32__GetTempFileNameA, "KERNEL32.dll!GetTempFileNameA" },
    { 0x007C3150u, imp_MSVCP60____Y__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV01_ABV01__Z, "MSVCP60.dll!??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@ABV01@@Z" },
    { 0x007C3154u, imp_MSVCP60___copy___char_traits_D_std__SAPADPADPBDI_Z, "MSVCP60.dll!?copy@?$char_traits@D@std@@SAPADPADPBDI@Z" },
    { 0x007C3158u, imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_ABV10_0_Z, "MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@0@Z" },
    { 0x007C315Cu, imp_MSVCP60____1__basic_fstream_DU__char_traits_D_std___std__UAE_XZ, "MSVCP60.dll!??1?$basic_fstream@DU?$char_traits@D@std@@@std@@UAE@XZ" },
    { 0x007C3160u, imp_MSVCP60____Y__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV01_PBD_Z, "MSVCP60.dll!??Y?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z" },
    { 0x007C3164u, imp_MSVCP60____0__basic_ios_DU__char_traits_D_std___std__IAE_XZ, "MSVCP60.dll!??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ" },
    { 0x007C3168u, imp_MSVCP60___clear_ios_base_std__QAEXH_N_Z, "MSVCP60.dll!?clear@ios_base@std@@QAEXH_N@Z" },
    { 0x007C316Cu, imp_MSVCP60____6std__YAAAV__basic_ostream_DU__char_traits_D_std___0_AAV10_PBD_Z, "MSVCP60.dll!??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z" },
    { 0x007C3170u, imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_PBDABV10__Z, "MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBDABV10@@Z" },
    { 0x007C3174u, imp_MSVCP60___find_first_not_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z, "MSVCP60.dll!?find_first_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z" },
    { 0x007C3178u, imp_MSVCP60___max_size___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIXZ, "MSVCP60.dll!?max_size@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIXZ" },
    { 0x007C317Cu, imp_MSVCP60___find___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z, "MSVCP60.dll!?find@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z" },
    { 0x007C3180u, imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_ABV10_D_Z, "MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@D@Z" },
    { 0x007C3184u, imp_MSVCP60_____F__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEXXZ, "MSVCP60.dll!??_F?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXXZ" },
    { 0x007C3188u, imp_MSVCP60____9std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z, "MSVCP60.dll!??9std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z" },
    { 0x007C318Cu, imp_MSVCP60____Ostd__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z, "MSVCP60.dll!??Ostd@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z" },
    { 0x007C3190u, imp_MSVCP60____8std__YA_NPBDABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0__Z, "MSVCP60.dll!??8std@@YA_NPBDABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@@Z" },
    { 0x007C3194u, imp_MSVCP60___replace___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_IIABV12_II_Z, "MSVCP60.dll!?replace@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@IIABV12@II@Z" },
    { 0x007C3198u, imp_MSVCP60___find_last_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z, "MSVCP60.dll!?find_last_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z" },
    { 0x007C319Cu, imp_MSVCP60___find_last_not_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z, "MSVCP60.dll!?find_last_not_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z" },
    { 0x007C31A0u, imp_MSVCP60___copy___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPADII_Z, "MSVCP60.dll!?copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPADII@Z" },
    { 0x007C31A4u, imp_MSVCP60____Copy___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXI_Z, "MSVCP60.dll!?_Copy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z" },
    { 0x007C31A8u, imp_MSVCP60____Xran_std__YAXXZ, "MSVCP60.dll!?_Xran@std@@YAXXZ" },
    { 0x007C31ACu, imp_MSVCP60____Split___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXXZ, "MSVCP60.dll!?_Split@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ" },
    { 0x007C31B0u, imp_MSVCP60___setstate___basic_ios_DU__char_traits_D_std___std__QAEXH_N_Z, "MSVCP60.dll!?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z" },
    { 0x007C31B4u, imp_MSVCP60____6std__YAAAV__basic_ostream_DU__char_traits_D_std___0_AAV10_ABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0__Z, "MSVCP60.dll!??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@@Z" },
    { 0x007C31B8u, imp_MSVCP60_____7__basic_filebuf_DU__char_traits_D_std___std__6B_, "MSVCP60.dll!??_7?$basic_filebuf@DU?$char_traits@D@std@@@std@@6B@" },
    { 0x007C31BCu, imp_MSVCP60___close___basic_filebuf_DU__char_traits_D_std___std__QAEPAV12_XZ, "MSVCP60.dll!?close@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@XZ" },
    { 0x007C31C0u, imp_MSVCP60____1locale_std__QAE_XZ, "MSVCP60.dll!??1locale@std@@QAE@XZ" },
    { 0x007C31C4u, imp_MSVCP60____1__basic_streambuf_DU__char_traits_D_std___std__UAE_XZ, "MSVCP60.dll!??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAE@XZ" },
    { 0x007C31C8u, imp_MSVCP60___append___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_ID_Z, "MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ID@Z" },
    { 0x007C31CCu, imp_MSVCP60___append___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_PBDI_Z, "MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z" },
    { 0x007C31D0u, imp_MSVCP60___append___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_ABV12_II_Z, "MSVCP60.dll!?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z" },
    { 0x007C31D4u, imp_MSVCP60_____8__basic_fstream_DU__char_traits_D_std___std__7B__basic_istream_DU__char_traits_D_std___1__, "MSVCP60.dll!??_8?$basic_fstream@DU?$char_traits@D@std@@@std@@7B?$basic_istream@DU?$char_traits@D@std@@@1@@" },
    { 0x007C31D8u, imp_MSVCP60___length___char_traits_D_std__SAIPBD_Z, "MSVCP60.dll!?length@?$char_traits@D@std@@SAIPBD@Z" },
    { 0x007C31DCu, imp_MSVCP60____Xlen_std__YAXXZ, "MSVCP60.dll!?_Xlen@std@@YAXXZ" },
    { 0x007C31E0u, imp_MSVCP60____4__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV01_PBD_Z, "MSVCP60.dll!??4?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV01@PBD@Z" },
    { 0x007C31E4u, imp_MSVCP60___assign___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_PBD_Z, "MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBD@Z" },
    { 0x007C31E8u, imp_MSVCP60_____8__basic_fstream_DU__char_traits_D_std___std__7B__basic_ostream_DU__char_traits_D_std___1__, "MSVCP60.dll!??_8?$basic_fstream@DU?$char_traits@D@std@@@std@@7B?$basic_ostream@DU?$char_traits@D@std@@@1@@" },
    { 0x007C31ECu, imp_MSVCP60____0__basic_iostream_DU__char_traits_D_std___std__QAE_PAV__basic_streambuf_DU__char_traits_D_std___1__Z, "MSVCP60.dll!??0?$basic_iostream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z" },
    { 0x007C31F0u, imp_MSVCP60____0ios_base_std__IAE_XZ, "MSVCP60.dll!??0ios_base@std@@IAE@XZ" },
    { 0x007C31F4u, imp_MSVCP60_____7__basic_ios_DU__char_traits_D_std___std__6B_, "MSVCP60.dll!??_7?$basic_ios@DU?$char_traits@D@std@@@std@@6B@" },
    { 0x007C31F8u, imp_MSVCP60___open___basic_filebuf_DU__char_traits_D_std___std__QAEPAV12_PBDH_Z, "MSVCP60.dll!?open@?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAEPAV12@PBDH@Z" },
    { 0x007C31FCu, imp_MSVCP60____0__basic_filebuf_DU__char_traits_D_std___std__QAE_PAU_iobuf___Z, "MSVCP60.dll!??0?$basic_filebuf@DU?$char_traits@D@std@@@std@@QAE@PAU_iobuf@@@Z" },
    { 0x007C3200u, imp_MSVCP60_____7__basic_fstream_DU__char_traits_D_std___std__6B_, "MSVCP60.dll!??_7?$basic_fstream@DU?$char_traits@D@std@@@std@@6B@" },
    { 0x007C3204u, imp_MSVCP60____1__basic_iostream_DU__char_traits_D_std___std__UAE_XZ, "MSVCP60.dll!??1?$basic_iostream@DU?$char_traits@D@std@@@std@@UAE@XZ" },
    { 0x007C3208u, imp_MSVCP60___clear___basic_ios_DU__char_traits_D_std___std__QAEXH_N_Z, "MSVCP60.dll!?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z" },
    { 0x007C320Cu, imp_MSVCP60____1__basic_filebuf_DU__char_traits_D_std___std__UAE_XZ, "MSVCP60.dll!??1?$basic_filebuf@DU?$char_traits@D@std@@@std@@UAE@XZ" },
    { 0x007C3210u, imp_MSVCP60_____D__basic_fstream_DU__char_traits_D_std___std__QAEXXZ, "MSVCP60.dll!??_D?$basic_fstream@DU?$char_traits@D@std@@@std@@QAEXXZ" },
    { 0x007C3214u, imp_MSVCP60____1ios_base_std__UAE_XZ, "MSVCP60.dll!??1ios_base@std@@UAE@XZ" },
    { 0x007C3218u, imp_MSVCP60____1__basic_ios_DU__char_traits_D_std___std__UAE_XZ, "MSVCP60.dll!??1?$basic_ios@DU?$char_traits@D@std@@@std@@UAE@XZ" },
    { 0x007C321Cu, imp_MSVCP60____1Init_ios_base_std__QAE_XZ, "MSVCP60.dll!??1Init@ios_base@std@@QAE@XZ" },
    { 0x007C3220u, imp_MSVCP60____1_Winit_std__QAE_XZ, "MSVCP60.dll!??1_Winit@std@@QAE@XZ" },
    { 0x007C3224u, imp_MSVCP60____0_Winit_std__QAE_XZ, "MSVCP60.dll!??0_Winit@std@@QAE@XZ" },
    { 0x007C3228u, imp_MSVCP60___find_first_of___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEIPBDII_Z, "MSVCP60.dll!?find_first_of@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z" },
    { 0x007C322Cu, imp_MSVCP60____0Init_ios_base_std__QAE_XZ, "MSVCP60.dll!??0Init@ios_base@std@@QAE@XZ" },
    { 0x007C3230u, imp_MSVCP60___substr___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBE_AV12_II_Z, "MSVCP60.dll!?substr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBE?AV12@II@Z" },
    { 0x007C3234u, imp_MSVCP60____Hstd__YA_AV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_ABV10_PBD_Z, "MSVCP60.dll!??Hstd@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@ABV10@PBD@Z" },
    { 0x007C3238u, imp_MSVCP60____0_Lockit_std__QAE_XZ, "MSVCP60.dll!??0_Lockit@std@@QAE@XZ" },
    { 0x007C323Cu, imp_MSVCP60___assign___char_traits_D_std__SAXAADABD_Z, "MSVCP60.dll!?assign@?$char_traits@D@std@@SAXAADABD@Z" },
    { 0x007C3240u, imp_MSVCP60____Mstd__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z, "MSVCP60.dll!??Mstd@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z" },
    { 0x007C3244u, imp_MSVCP60____1_Lockit_std__QAE_XZ, "MSVCP60.dll!??1_Lockit@std@@QAE@XZ" },
    { 0x007C3248u, imp_MSVCP60___npos___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__2IB, "MSVCP60.dll!?npos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@2IB" },
    { 0x007C324Cu, imp_MSVCP60___erase___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_II_Z, "MSVCP60.dll!?erase@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@II@Z" },
    { 0x007C3250u, imp_MSVCP60____Grow___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAE_NI_N_Z, "MSVCP60.dll!?_Grow@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAE_NI_N@Z" },
    { 0x007C3254u, imp_MSVCP60____Eos___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXI_Z, "MSVCP60.dll!?_Eos@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXI@Z" },
    { 0x007C3258u, imp_MSVCP60___assign___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_ABV12_II_Z, "MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@II@Z" },
    { 0x007C325Cu, imp_MSVCP60____1__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_XZ, "MSVCP60.dll!??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ" },
    { 0x007C3260u, imp_MSVCP60____C__1___Nullstr___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__CAPBDXZ_4DB, "MSVCP60.dll!?_C@?1??_Nullstr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@CAPBDXZ@4DB" },
    { 0x007C3264u, imp_MSVCP60____Tidy___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEX_N_Z, "MSVCP60.dll!?_Tidy@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEX_N@Z" },
    { 0x007C3268u, imp_MSVCP60___assign___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEAAV12_PBDI_Z, "MSVCP60.dll!?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z" },
    { 0x007C326Cu, imp_MSVCP60____0__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_ABV__allocator_D_1__Z, "MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV?$allocator@D@1@@Z" },
    { 0x007C3270u, imp_MSVCP60____9std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_PBD_Z, "MSVCP60.dll!??9std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z" },
    { 0x007C3274u, imp_MSVCP60____8std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_PBD_Z, "MSVCP60.dll!??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z" },
    { 0x007C3278u, imp_MSVCP60____0__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_PBDABV__allocator_D_1__Z, "MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDABV?$allocator@D@1@@Z" },
    { 0x007C327Cu, imp_MSVCP60____0__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAE_ABV01__Z, "MSVCP60.dll!??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z" },
    { 0x007C3280u, imp_MSVCP60____A__basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QBEABDI_Z, "MSVCP60.dll!??A?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEABDI@Z" },
    { 0x007C3284u, imp_MSVCP60___nothrow_std__3Unothrow_t_1_B, "MSVCP60.dll!?nothrow@std@@3Unothrow_t@1@B" },
    { 0x007C3288u, imp_MSVCP60____8std__YA_NABV__basic_string_DU__char_traits_D_std__V__allocator_D_2__0_0_Z, "MSVCP60.dll!??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@0@Z" },
    { 0x007C328Cu, imp_MSVCP60____Refcnt___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEAAEPBD_Z, "MSVCP60.dll!?_Refcnt@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEAAEPBD@Z" },
    { 0x007C3290u, imp_MSVCP60____Freeze___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__AAEXXZ, "MSVCP60.dll!?_Freeze@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ" },
    { 0x007C3294u, imp_MSVCP60___resize___basic_string_DU__char_traits_D_std__V__allocator_D_2__std__QAEXI_Z, "MSVCP60.dll!?resize@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEXI@Z" },
    { 0x007C329Cu, imp_MSVCRT___terminate__YAXXZ, "MSVCRT.dll!?terminate@@YAXXZ" },
    { 0x007C32A0u, imp_MSVCRT___onexit, "MSVCRT.dll!_onexit" },
    { 0x007C32A4u, imp_MSVCRT____dllonexit, "MSVCRT.dll!__dllonexit" },
    { 0x007C32A8u, imp_MSVCRT___except_handler3, "MSVCRT.dll!_except_handler3" },
    { 0x007C32ACu, imp_MSVCRT____set_app_type, "MSVCRT.dll!__set_app_type" },
    { 0x007C32B0u, imp_MSVCRT____p__fmode, "MSVCRT.dll!__p__fmode" },
    { 0x007C32B4u, imp_MSVCRT____p__commode, "MSVCRT.dll!__p__commode" },
    { 0x007C32B8u, imp_MSVCRT___adjust_fdiv, "MSVCRT.dll!_adjust_fdiv" },
    { 0x007C32BCu, imp_MSVCRT____setusermatherr, "MSVCRT.dll!__setusermatherr" },
    { 0x007C32C0u, imp_MSVCRT___initterm, "MSVCRT.dll!_initterm" },
    { 0x007C32C4u, imp_MSVCRT____getmainargs, "MSVCRT.dll!__getmainargs" },
    { 0x007C32C8u, imp_MSVCRT__exit, "MSVCRT.dll!exit" },
    { 0x007C32CCu, imp_MSVCRT___XcptFilter, "MSVCRT.dll!_XcptFilter" },
    { 0x007C32D0u, imp_MSVCRT___exit, "MSVCRT.dll!_exit" },
    { 0x007C32D4u, imp_MSVCRT____1type_info__UAE_XZ, "MSVCRT.dll!??1type_info@@UAE@XZ" },
    { 0x007C32D8u, imp_MSVCRT__fgets, "MSVCRT.dll!fgets" },
    { 0x007C32DCu, imp_MSVCRT__fseek, "MSVCRT.dll!fseek" },
    { 0x007C32E0u, imp_MSVCRT__ftell, "MSVCRT.dll!ftell" },
    { 0x007C32E4u, imp_MSVCRT__isalpha, "MSVCRT.dll!isalpha" },
    { 0x007C32E8u, imp_MSVCRT__strncmp, "MSVCRT.dll!strncmp" },
    { 0x007C32ECu, imp_MSVCRT__mktime, "MSVCRT.dll!mktime" },
    { 0x007C32F0u, imp_MSVCRT___controlfp, "MSVCRT.dll!_controlfp" },
    { 0x007C32F4u, imp_MSVCRT___splitpath, "MSVCRT.dll!_splitpath" },
    { 0x007C32F8u, imp_MSVCRT___fullpath, "MSVCRT.dll!_fullpath" },
    { 0x007C32FCu, imp_MSVCRT__memchr, "MSVCRT.dll!memchr" },
    { 0x007C3300u, imp_MSVCRT__atoi, "MSVCRT.dll!atoi" },
    { 0x007C3304u, imp_MSVCRT__strncpy, "MSVCRT.dll!strncpy" },
    { 0x007C3308u, imp_MSVCRT___stricmp, "MSVCRT.dll!_stricmp" },
    { 0x007C330Cu, imp_MSVCRT__vsprintf, "MSVCRT.dll!vsprintf" },
    { 0x007C3310u, imp_MSVCRT__fprintf, "MSVCRT.dll!fprintf" },
    { 0x007C3314u, imp_MSVCRT__fopen, "MSVCRT.dll!fopen" },
    { 0x007C3318u, imp_MSVCRT__fread, "MSVCRT.dll!fread" },
    { 0x007C331Cu, imp_MSVCRT__fclose, "MSVCRT.dll!fclose" },
    { 0x007C3320u, imp_MSVCRT__ceil, "MSVCRT.dll!ceil" },
    { 0x007C3324u, imp_MSVCRT___purecall, "MSVCRT.dll!_purecall" },
    { 0x007C3328u, imp_MSVCRT__sscanf, "MSVCRT.dll!sscanf" },
    { 0x007C332Cu, imp_MSVCRT___CIacos, "MSVCRT.dll!_CIacos" },
    { 0x007C3330u, imp_MSVCRT___CIasin, "MSVCRT.dll!_CIasin" },
    { 0x007C3334u, imp_MSVCRT____RTDynamicCast, "MSVCRT.dll!__RTDynamicCast" },
    { 0x007C3338u, imp_MSVCRT__memmove, "MSVCRT.dll!memmove" },
    { 0x007C333Cu, imp_MSVCRT__sprintf, "MSVCRT.dll!sprintf" },
    { 0x007C3340u, imp_MSVCRT___ftol, "MSVCRT.dll!_ftol" },
    { 0x007C3344u, imp_MSVCRT____2_YAPAXI_Z, "MSVCRT.dll!??2@YAPAXI@Z" },
    { 0x007C3348u, imp_MSVCRT____CxxFrameHandler, "MSVCRT.dll!__CxxFrameHandler" },
    { 0x007C334Cu, imp_MSVCRT__free, "MSVCRT.dll!free" },
    { 0x007C3350u, imp_MSVCRT__malloc, "MSVCRT.dll!malloc" },
    { 0x007C3354u, imp_MSVCRT___CxxThrowException, "MSVCRT.dll!_CxxThrowException" },
    { 0x007C3358u, imp_MSVCRT___acmdln, "MSVCRT.dll!_acmdln" },
    { 0x007C335Cu, imp_MSVCRT__atof, "MSVCRT.dll!atof" },
    { 0x007C3360u, imp_MSVCRT__bsearch, "MSVCRT.dll!bsearch" },
    { 0x007C3364u, imp_MSVCRT___strnicmp, "MSVCRT.dll!_strnicmp" },
    { 0x007C3368u, imp_MSVCRT___CIfmod, "MSVCRT.dll!_CIfmod" },
    { 0x007C336Cu, imp_MSVCRT____8type_info__QBEHABV0__Z, "MSVCRT.dll!??8type_info@@QBEHABV0@@Z" },
    { 0x007C3370u, imp_MSVCRT__floor, "MSVCRT.dll!floor" },
    { 0x007C3374u, imp_MSVCRT__toupper, "MSVCRT.dll!toupper" },
    { 0x007C3378u, imp_MSVCRT___CIpow, "MSVCRT.dll!_CIpow" },
    { 0x007C337Cu, imp_MSVCRT____RTtypeid, "MSVCRT.dll!__RTtypeid" },
    { 0x007C3380u, imp_MSVCRT__isdigit, "MSVCRT.dll!isdigit" },
    { 0x007C3384u, imp_MSVCRT__isxdigit, "MSVCRT.dll!isxdigit" },
    { 0x007C3388u, imp_MSVCRT__tolower, "MSVCRT.dll!tolower" },
    { 0x007C338Cu, imp_MSVCRT__isupper, "MSVCRT.dll!isupper" },
    { 0x007C3390u, imp_MSVCRT__islower, "MSVCRT.dll!islower" },
    { 0x007C3394u, imp_MSVCRT__isalnum, "MSVCRT.dll!isalnum" },
    { 0x007C3398u, imp_MSVCRT__isspace, "MSVCRT.dll!isspace" },
    { 0x007C339Cu, imp_MSVCRT___callnewh, "MSVCRT.dll!_callnewh" },
    { 0x007C33A0u, imp_MSVCRT__isprint, "MSVCRT.dll!isprint" },
    { 0x007C33A4u, imp_MSVCRT__realloc, "MSVCRT.dll!realloc" },
    { 0x007C33ACu, imp_SHELL32__ShellExecuteA, "SHELL32.dll!ShellExecuteA" },
    { 0x007C33B4u, imp_USER32__CreateWindowExA, "USER32.dll!CreateWindowExA" },
    { 0x007C33B8u, imp_USER32__CharPrevA, "USER32.dll!CharPrevA" },
    { 0x007C33BCu, imp_USER32__PostThreadMessageA, "USER32.dll!PostThreadMessageA" },
    { 0x007C33C0u, imp_USER32__SetWindowTextA, "USER32.dll!SetWindowTextA" },
    { 0x007C33C4u, imp_USER32__SetWindowPos, "USER32.dll!SetWindowPos" },
    { 0x007C33C8u, imp_USER32__GetSystemMetrics, "USER32.dll!GetSystemMetrics" },
    { 0x007C33CCu, imp_USER32__EndPaint, "USER32.dll!EndPaint" },
    { 0x007C33D0u, imp_USER32__CreateDialogParamA, "USER32.dll!CreateDialogParamA" },
    { 0x007C33D4u, imp_USER32__SetCursor, "USER32.dll!SetCursor" },
    { 0x007C33D8u, imp_USER32__PeekMessageA, "USER32.dll!PeekMessageA" },
    { 0x007C33DCu, imp_USER32__GetWindowTextA, "USER32.dll!GetWindowTextA" },
    { 0x007C33E0u, imp_USER32__MsgWaitForMultipleObjects, "USER32.dll!MsgWaitForMultipleObjects" },
    { 0x007C33E4u, imp_USER32__DispatchMessageA, "USER32.dll!DispatchMessageA" },
    { 0x007C33E8u, imp_USER32__GetMessageA, "USER32.dll!GetMessageA" },
    { 0x007C33ECu, imp_USER32__TranslateMessage, "USER32.dll!TranslateMessage" },
    { 0x007C33F0u, imp_USER32__RegisterClassExA, "USER32.dll!RegisterClassExA" },
    { 0x007C33F4u, imp_USER32__CharNextA, "USER32.dll!CharNextA" },
    { 0x007C33F8u, imp_USER32__ShowWindow, "USER32.dll!ShowWindow" },
    { 0x007C33FCu, imp_USER32__GetWindowRect, "USER32.dll!GetWindowRect" },
    { 0x007C3400u, imp_USER32__BeginPaint, "USER32.dll!BeginPaint" },
    { 0x007C3404u, imp_USER32__GetUpdateRect, "USER32.dll!GetUpdateRect" },
    { 0x007C3408u, imp_USER32__LoadBitmapA, "USER32.dll!LoadBitmapA" },
    { 0x007C340Cu, imp_USER32__LoadCursorA, "USER32.dll!LoadCursorA" },
    { 0x007C3410u, imp_USER32__MessageBoxA, "USER32.dll!MessageBoxA" },
    { 0x007C3414u, imp_USER32__UpdateWindow, "USER32.dll!UpdateWindow" },
    { 0x007C3418u, imp_USER32__IsWindowVisible, "USER32.dll!IsWindowVisible" },
    { 0x007C341Cu, imp_USER32__SetParent, "USER32.dll!SetParent" },
    { 0x007C3420u, imp_USER32__DestroyWindow, "USER32.dll!DestroyWindow" },
    { 0x007C3424u, imp_USER32__IsWindow, "USER32.dll!IsWindow" },
    { 0x007C3428u, imp_USER32__IsIconic, "USER32.dll!IsIconic" },
    { 0x007C342Cu, imp_USER32__GetDesktopWindow, "USER32.dll!GetDesktopWindow" },
    { 0x007C3430u, imp_USER32__ClientToScreen, "USER32.dll!ClientToScreen" },
    { 0x007C3434u, imp_USER32__SetCursorPos, "USER32.dll!SetCursorPos" },
    { 0x007C3438u, imp_USER32__GetKeyState, "USER32.dll!GetKeyState" },
    { 0x007C343Cu, imp_USER32__ShowCursor, "USER32.dll!ShowCursor" },
    { 0x007C3440u, imp_USER32__GetParent, "USER32.dll!GetParent" },
    { 0x007C3444u, imp_USER32__SetWindowPlacement, "USER32.dll!SetWindowPlacement" },
    { 0x007C3448u, imp_USER32__GetWindowLongA, "USER32.dll!GetWindowLongA" },
    { 0x007C344Cu, imp_USER32__GetWindowPlacement, "USER32.dll!GetWindowPlacement" },
    { 0x007C3450u, imp_USER32__SetWindowLongA, "USER32.dll!SetWindowLongA" },
    { 0x007C3454u, imp_USER32__GetClientRect, "USER32.dll!GetClientRect" },
    { 0x007C3458u, imp_USER32__DefWindowProcA, "USER32.dll!DefWindowProcA" },
    { 0x007C345Cu, imp_USER32__SetPropA, "USER32.dll!SetPropA" },
    { 0x007C3460u, imp_USER32__GetPropA, "USER32.dll!GetPropA" },
    { 0x007C3468u, imp_WINMM__timeSetEvent, "WINMM.dll!timeSetEvent" },
    { 0x007C346Cu, imp_WINMM__timeEndPeriod, "WINMM.dll!timeEndPeriod" },
    { 0x007C3470u, imp_WINMM__timeGetTime, "WINMM.dll!timeGetTime" },
    { 0x007C3474u, imp_WINMM__mciSendStringA, "WINMM.dll!mciSendStringA" },
    { 0x007C3478u, imp_WINMM__PlaySoundA, "WINMM.dll!PlaySoundA" },
    { 0x007C347Cu, imp_WINMM__timeKillEvent, "WINMM.dll!timeKillEvent" },
    { 0x007C3480u, imp_WINMM__timeBeginPeriod, "WINMM.dll!timeBeginPeriod" },
    { 0x007C3484u, imp_WINMM__timeGetDevCaps, "WINMM.dll!timeGetDevCaps" },
    { 0x007C348Cu, imp_mss32___AIL_startup_0, "mss32.dll!_AIL_startup@0" },
    { 0x007C3490u, imp_mss32___AIL_digital_handle_release_4, "mss32.dll!_AIL_digital_handle_release@4" },
    { 0x007C3494u, imp_mss32___AIL_digital_handle_reacquire_4, "mss32.dll!_AIL_digital_handle_reacquire@4" },
    { 0x007C3498u, imp_mss32___AIL_allocate_sample_handle_4, "mss32.dll!_AIL_allocate_sample_handle@4" },
    { 0x007C349Cu, imp_mss32___AIL_set_DirectSound_HWND_8, "mss32.dll!_AIL_set_DirectSound_HWND@8" },
    { 0x007C34A0u, imp_mss32___AIL_last_error_0, "mss32.dll!_AIL_last_error@0" },
    { 0x007C34A4u, imp_mss32___AIL_waveOutOpen_16, "mss32.dll!_AIL_waveOutOpen@16" },
    { 0x007C34A8u, imp_mss32___AIL_set_preference_8, "mss32.dll!_AIL_set_preference@8" },
    { 0x007C34ACu, imp_mss32___AIL_set_sample_pan_8, "mss32.dll!_AIL_set_sample_pan@8" },
    { 0x007C34B0u, imp_mss32___AIL_shutdown_0, "mss32.dll!_AIL_shutdown@0" },
    { 0x007C34B4u, imp_mss32___AIL_waveOutClose_4, "mss32.dll!_AIL_waveOutClose@4" },
    { 0x007C34B8u, imp_mss32___AIL_release_sample_handle_4, "mss32.dll!_AIL_release_sample_handle@4" },
    { 0x007C34BCu, imp_mss32___AIL_get_DirectSound_info_12, "mss32.dll!_AIL_get_DirectSound_info@12" },
    { 0x007C34C0u, imp_mss32___AIL_set_sample_playback_rate_8, "mss32.dll!_AIL_set_sample_playback_rate@8" },
    { 0x007C34C4u, imp_mss32___AIL_set_named_sample_file_20, "mss32.dll!_AIL_set_named_sample_file@20" },
    { 0x007C34C8u, imp_mss32___AIL_start_sample_4, "mss32.dll!_AIL_start_sample@4" },
    { 0x007C34CCu, imp_mss32___AIL_set_sample_loop_count_8, "mss32.dll!_AIL_set_sample_loop_count@8" },
    { 0x007C34D0u, imp_mss32___AIL_set_sample_file_12, "mss32.dll!_AIL_set_sample_file@12" },
    { 0x007C34D4u, imp_mss32___AIL_set_sample_volume_8, "mss32.dll!_AIL_set_sample_volume@8" },
    { 0x007C34D8u, imp_mss32___AIL_end_sample_4, "mss32.dll!_AIL_end_sample@4" },
    { 0x007C34DCu, imp_mss32___AIL_init_sample_4, "mss32.dll!_AIL_init_sample@4" },
    { 0x007C34E0u, imp_mss32___AIL_sample_status_4, "mss32.dll!_AIL_sample_status@4" },
    { 0x007C34E8u, imp_ole32__CoInitialize, "ole32.dll!CoInitialize" },
    { 0x007C34ECu, imp_ole32__CoUninitialize, "ole32.dll!CoUninitialize" },
    { 0x007C34F0u, imp_ole32__CoCreateInstance, "ole32.dll!CoCreateInstance" },
};
const unsigned g_import_count = 307;
