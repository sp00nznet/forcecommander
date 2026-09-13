/*
 * Force Commander - C runtime and KERNEL32 import shims.
 *
 * Focom.exe links the CRT dynamically (67 MSVCRT imports, 82 MSVCP60), so these
 * are import bridges rather than lifted functions -- which is the easier half of
 * the deal, because most of them can pass straight through to the host CRT.
 *
 * Two things do not pass through, and they are the whole reason this file is
 * more than a table:
 *
 *   Pointers. Anything the lifted code will dereference has to live inside the
 *   target's 32-bit address space, because the lifted code reads it through
 *   MEM32(va). A host malloc returns a 64-bit pointer outside that space, so
 *   the allocator here hands out VAs from the runtime's own heap region.
 *
 *   Opaque handles. FILE* is a host pointer; the lifted code holds 32 bits. Same
 *   table trick as the GDI handles in shims_impl.c.
 *
 * Everything here is cdecl: the caller cleans the arguments, so each shim ends
 * with CDECLRET(), which drops only the dummy return address. Using STDRET(k)
 * on one of these unbalances the stack by exactly the argument count, and the
 * symptom appears nowhere near the cause.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#include "recomp_types.h"
#include "imports.h"

/* ------------------------------------------------------------- allocator */

/*
 * A bump allocator with a size prefix and no reuse. Deliberately the dumbest
 * thing that works: during bring-up the interesting failures are wrong
 * pointers, not fragmentation, and a simple allocator never obscures one. The
 * heap is 128 MB; if a real run exhausts it, that is the signal to write a real
 * one rather than a reason to have written one now.
 *
 * ponytail: bump-only, no free list. Swap in a real allocator when a run
 * actually runs out, not before.
 */
#define HEAP_BASE 0x10000000u
#define HEAP_SIZE 0x08000000u

static uint32_t heap_next = HEAP_BASE + 16;
static uint32_t heap_peak = 0;
static unsigned heap_allocs = 0;

uint32_t crt_alloc(uint32_t n) {
    if (!n) n = 1;
    uint32_t hdr = (heap_next + 15) & ~15u;      /* 16-byte aligned payload */
    uint32_t ptr = hdr + 16;
    if (ptr + n > HEAP_BASE + HEAP_SIZE) {
        fprintf(stderr, "[crt] heap exhausted at %u bytes (%u allocations)\n",
                heap_peak, heap_allocs);
        return 0;
    }
    MEM32(hdr) = n;                              /* size, for realloc */
    heap_next = ptr + n;
    heap_peak = heap_next - HEAP_BASE;
    heap_allocs++;
    return ptr;
}

static uint32_t crt_size_of(uint32_t ptr) {
    return ptr ? MEM32(ptr - 16) : 0;
}

void crt_heap_stats(void) {
    printf("  heap: %u allocations, %u bytes high-water\n", heap_allocs, heap_peak);
}

/* ---------------------------------------------------------- FILE handles */

#define FT_MAX 128
static FILE* ft[FT_MAX];
static unsigned ft_n = 1;

static uint32_t f2i(FILE* f) {
    if (!f) return 0;
    if (ft_n >= FT_MAX) return 0;
    ft[ft_n] = f;
    return ft_n++;
}
static FILE* i2f(uint32_t i) { return (i && i < ft_n) ? ft[i] : NULL; }

/* Where the game's data actually lives, for translating its relative paths. */
static char g_gamedir[MAX_PATH] = "game";

void crt_set_gamedir(const char* d) {
    if (d) { strncpy(g_gamedir, d, sizeof(g_gamedir) - 1); g_gamedir[sizeof(g_gamedir)-1] = 0; }
}

static const char* host_path(uint32_t va, char* buf, size_t n) {
    const char* p = (const char*)(uintptr_t)ADDR(va);
    if (!p || !*p) return NULL;
    /* Absolute or drive-qualified paths are used as given. */
    if (p[0] == '\\' || p[0] == '/' || (p[1] == ':' )) { snprintf(buf, n, "%s", p); return buf; }
    snprintf(buf, n, "%s/%s", g_gamedir, p);
    return buf;
}

/* ------------------------------------------------------------- varargs */

/*
 * The printf family takes its arguments off the simulated stack. Rather than
 * reimplement format parsing, walk the format string to learn each conversion's
 * width, pull the slots, and hand them to the host vsnprintf one conversion at
 * a time. Slower than the real thing and immeasurably simpler to get right --
 * and a format bug here would otherwise look like a game bug.
 */
static int crt_format(char* out, size_t outsz, const char* fmt, uint32_t argva) {
    size_t o = 0;
    uint32_t a = argva;
    for (const char* p = fmt; *p && o + 1 < outsz; ) {
        if (*p != '%') { out[o++] = *p++; continue; }
        if (p[1] == '%') { out[o++] = '%'; p += 2; continue; }

        const char* start = p++;
        while (*p && !strchr("diouxXeEfgGaAcspn%", *p)) p++;
        char conv = *p ? *p++ : 's';
        char spec[64];
        size_t sl = (size_t)(p - start);
        if (sl >= sizeof(spec)) sl = sizeof(spec) - 1;
        memcpy(spec, start, sl);
        spec[sl] = 0;

        char tmp[1024];
        int wrote = 0;
        if (conv == 's') {
            uint32_t sva = MEM32(a); a += 4;
            const char* s = sva ? (const char*)(uintptr_t)ADDR(sva) : "(null)";
            wrote = snprintf(tmp, sizeof(tmp), spec, s);
        } else if (conv == 'f' || conv == 'e' || conv == 'E' || conv == 'g' ||
                   conv == 'G' || conv == 'a' || conv == 'A') {
            /* A double occupies two slots. */
            union { uint64_t u; double d; } v;
            v.u = (uint64_t)MEM32(a) | ((uint64_t)MEM32(a + 4) << 32);
            a += 8;
            wrote = snprintf(tmp, sizeof(tmp), spec, v.d);
        } else if (conv == 'p') {
            wrote = snprintf(tmp, sizeof(tmp), "0x%08X", MEM32(a)); a += 4;
        } else if (conv == 'c') {
            wrote = snprintf(tmp, sizeof(tmp), spec, (int)(MEM32(a) & 0xFF)); a += 4;
        } else {
            wrote = snprintf(tmp, sizeof(tmp), spec, (int)MEM32(a)); a += 4;
        }
        if (wrote < 0) wrote = 0;
        for (int i = 0; i < wrote && o + 1 < outsz; i++) out[o++] = tmp[i];
    }
    out[o] = 0;
    return (int)o;
}

/* ----------------------------------------------------- x87 helpers

 * The `_st` / `_fp_top` aliases and RECOMP_CALL only exist under
 * RECOMP_GENERATED_CODE, which this file is not. Use the globals directly.
 */
static void fpush(double v) { g_fp_top = (g_fp_top - 1) & 7; g_st[g_fp_top] = v; }
static double fpeek(int i)  { return g_st[(g_fp_top + i) & 7]; }
static void fpop(void)      { g_fp_top = (g_fp_top + 1) & 7; }

/* Call a lifted function the way RECOMP_CALL would: push the dummy return
 * address the callee's `ret` will pop. */
static void call_lifted(recomp_func_t f) {
    uint32_t caller = g_cur_func;
    PUSH32(g_esp, RECOMP_RETADDR);
    f();
    g_cur_func = caller;
}

/* ------------------------------------------------------------- the shims */

/* memory */
static void crt_malloc(void)  { RET(crt_alloc(ARG(0))); CDECLRET(); }
static void crt_free(void)    { (void)ARG(0); RET(0); CDECLRET(); }
static void crt_calloc(void)  {
    uint32_t n = ARG(0) * ARG(1), p = crt_alloc(n);
    if (p) memset((void*)(uintptr_t)ADDR(p), 0, n);
    RET(p); CDECLRET();
}
static void crt_realloc(void) {
    uint32_t old = ARG(0), n = ARG(1);
    if (!old) { RET(crt_alloc(n)); CDECLRET(); return; }
    uint32_t osz = crt_size_of(old), p = crt_alloc(n);
    if (p) memcpy((void*)(uintptr_t)ADDR(p), (void*)(uintptr_t)ADDR(old),
                  osz < n ? osz : n);
    RET(p); CDECLRET();
}
/* operator new / delete are MSVCRT exports too. */
static void crt_new(void)     { RET(crt_alloc(ARG(0))); CDECLRET(); }

/* string / memory */
static void crt_memcpy(void)  { memcpy(ARGP(0,char), ARGP(1,char), ARG(2)); RET(ARG(0)); CDECLRET(); }
static void crt_memmove(void) { memmove(ARGP(0,char), ARGP(1,char), ARG(2)); RET(ARG(0)); CDECLRET(); }
static void crt_memset(void)  { memset(ARGP(0,char), (int)ARG(1), ARG(2)); RET(ARG(0)); CDECLRET(); }
static void crt_memcmp(void)  { RET((uint32_t)memcmp(ARGP(0,char), ARGP(1,char), ARG(2))); CDECLRET(); }
static void crt_memchr(void)  {
    void* r = memchr(ARGP(0,char), (int)ARG(1), ARG(2));
    RET(r ? ARG(0) + (uint32_t)((char*)r - ARGP(0,char)) : 0); CDECLRET();
}
static void crt_strlen(void)  { RET((uint32_t)strlen(ARGP(0,char))); CDECLRET(); }
static void crt_strcmp(void)  { RET((uint32_t)strcmp(ARGP(0,char), ARGP(1,char))); CDECLRET(); }
static void crt_strncmp(void) { RET((uint32_t)strncmp(ARGP(0,char), ARGP(1,char), ARG(2))); CDECLRET(); }
static void crt_stricmp(void) { RET((uint32_t)_stricmp(ARGP(0,char), ARGP(1,char))); CDECLRET(); }
static void crt_strnicmp(void){ RET((uint32_t)_strnicmp(ARGP(0,char), ARGP(1,char), ARG(2))); CDECLRET(); }
static void crt_strcpy(void)  { strcpy(ARGP(0,char), ARGP(1,char)); RET(ARG(0)); CDECLRET(); }
static void crt_strncpy(void) { strncpy(ARGP(0,char), ARGP(1,char), ARG(2)); RET(ARG(0)); CDECLRET(); }
static void crt_strcat(void)  { strcat(ARGP(0,char), ARGP(1,char)); RET(ARG(0)); CDECLRET(); }
static void crt_strchr(void)  {
    char* r = strchr(ARGP(0,char), (int)ARG(1));
    RET(r ? ARG(0) + (uint32_t)(r - ARGP(0,char)) : 0); CDECLRET();
}
static void crt_strrchr(void) {
    char* r = strrchr(ARGP(0,char), (int)ARG(1));
    RET(r ? ARG(0) + (uint32_t)(r - ARGP(0,char)) : 0); CDECLRET();
}
static void crt_strstr(void)  {
    char* r = strstr(ARGP(0,char), ARGP(1,char));
    RET(r ? ARG(0) + (uint32_t)(r - ARGP(0,char)) : 0); CDECLRET();
}

/* conversions + ctype */
static void crt_atoi(void)   { RET((uint32_t)atoi(ARGP(0,char))); CDECLRET(); }
static void crt_atof(void)   { fpush(atof(ARGP(0,char))); CDECLRET(); }
static void crt_toupper(void){ RET((uint32_t)toupper((int)ARG(0))); CDECLRET(); }
static void crt_tolower(void){ RET((uint32_t)tolower((int)ARG(0))); CDECLRET(); }
static void crt_isalpha(void){ RET(isalpha((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_isdigit(void){ RET(isdigit((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_isalnum(void){ RET(isalnum((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_isspace(void){ RET(isspace((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_isupper(void){ RET(isupper((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_islower(void){ RET(islower((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_isprint(void){ RET(isprint((int)ARG(0)) ? 1 : 0); CDECLRET(); }
static void crt_isxdigit(void){ RET(isxdigit((int)ARG(0)) ? 1 : 0); CDECLRET(); }

/* stdio */
static void crt_sprintf(void) {
    char buf[4096];
    int n = crt_format(buf, sizeof(buf), ARGP(1,char), g_esp + 4 + 2*4);
    strcpy(ARGP(0,char), buf);
    RET((uint32_t)n); CDECLRET();
}
static void crt_vsprintf(void) {
    char buf[4096];
    int n = crt_format(buf, sizeof(buf), ARGP(1,char), ARG(2));
    strcpy(ARGP(0,char), buf);
    RET((uint32_t)n); CDECLRET();
}
static void crt_fopen(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    FILE* f = hp ? fopen(hp, ARGP(1,char)) : NULL;
    if (!f) fprintf(stderr, "[crt] fopen(\"%s\") failed\n", hp ? hp : "(null)");
    RET(f2i(f)); CDECLRET();
}
static void crt_fclose(void) { FILE* f = i2f(ARG(0)); RET(f ? (uint32_t)fclose(f) : 0xFFFFFFFFu); CDECLRET(); }
static void crt_fread(void)  {
    FILE* f = i2f(ARG(3));
    RET(f ? (uint32_t)fread(ARGP(0,char), ARG(1), ARG(2), f) : 0); CDECLRET();
}
static void crt_fwrite(void) {
    FILE* f = i2f(ARG(3));
    RET(f ? (uint32_t)fwrite(ARGP(0,char), ARG(1), ARG(2), f) : 0); CDECLRET();
}
static void crt_fseek(void)  {
    FILE* f = i2f(ARG(0));
    RET(f ? (uint32_t)fseek(f, (long)(int32_t)ARG(1), (int)ARG(2)) : 0xFFFFFFFFu); CDECLRET();
}
static void crt_ftell(void)  { FILE* f = i2f(ARG(0)); RET(f ? (uint32_t)ftell(f) : 0xFFFFFFFFu); CDECLRET(); }
static void crt_fgets(void)  {
    FILE* f = i2f(ARG(2));
    RET(f && fgets(ARGP(0,char), (int)ARG(1), f) ? ARG(0) : 0); CDECLRET();
}
static void crt_fprintf(void) {
    char buf[4096];
    crt_format(buf, sizeof(buf), ARGP(1,char), g_esp + 4 + 2*4);
    FILE* f = i2f(ARG(0));
    fputs(buf, f ? f : stderr);
    RET((uint32_t)strlen(buf)); CDECLRET();
}

/* CRT startup. These get called before anything else and must not fail. */
static void crt_noop0(void)    { RET(0); CDECLRET(); }
static void crt_noop1(void)    { RET(1); CDECLRET(); }
static void crt_initterm(void) {
    /*
     * _initterm(first, last) walks a table of function pointers and calls each
     * non-null one. Those are the static constructors -- and in an STL-heavy
     * C++ program they are where basic_string and iostream initialisation
     * happens, so this is the first place the 58 refused MSVCP60 imports get
     * touched. Run them; a refusal will abort naming the exact symbol.
     */
    uint32_t p = ARG(0), end = ARG(1);
    unsigned n = 0;
    for (; p < end; p += 4) {
        uint32_t fn = MEM32(p);
        if (!fn) continue;
        recomp_func_t f = recomp_lookup(fn);
        if (!f) { fprintf(stderr, "[crt] _initterm: 0x%08X not lifted\n", fn); continue; }
        call_lifted(f);
        n++;
    }
    fprintf(stderr, "[crt] _initterm ran %u static constructors\n", n);
    RET(0); CDECLRET();
}

static uint32_t g_fmode_va, g_commode_va;
static void crt_p_fmode(void)   { if (!g_fmode_va) g_fmode_va = crt_alloc(4); RET(g_fmode_va); CDECLRET(); }
static void crt_p_commode(void) { if (!g_commode_va) g_commode_va = crt_alloc(4); RET(g_commode_va); CDECLRET(); }

static void crt_getmainargs(void) {
    /* __getmainargs(&argc, &argv, &env, doWildcard, startupinfo) */
    uint32_t argc_va = ARG(0), argv_va = ARG(1), env_va = ARG(2);
    uint32_t argv0 = crt_alloc(32);
    strcpy((char*)(uintptr_t)ADDR(argv0), "Focom.exe");
    uint32_t vec = crt_alloc(8);
    MEM32(vec) = argv0; MEM32(vec + 4) = 0;
    if (argc_va) MEM32(argc_va) = 1;
    if (argv_va) MEM32(argv_va) = vec;
    if (env_va)  MEM32(env_va) = 0;
    RET(0); CDECLRET();
}

static void crt_exit(void) {
    fprintf(stderr, "[crt] exit(%u) called by lifted code\n", ARG(0));
    crt_heap_stats();
    exit((int)ARG(0));
}

/* math intrinsics: args arrive on the x87 stack, result goes back on it */
static void crt_CIpow(void)  { double e = fpeek(0), b = fpeek(1);
                               fpop(); fpop(); fpush(pow(b, e)); CDECLRET(); }
static void crt_CIfmod(void) { double b = fpeek(0), a = fpeek(1);
                               fpop(); fpop(); fpush(fmod(a, b)); CDECLRET(); }
static void crt_CIacos(void) { double a = fpeek(0); fpop(); fpush(acos(a)); CDECLRET(); }
static void crt_CIasin(void) { double a = fpeek(0); fpop(); fpush(asin(a)); CDECLRET(); }
static void crt_floor(void)  { double a; uint64_t u = MEM64(g_esp+4); memcpy(&a,&u,8); fpush(floor(a)); CDECLRET(); }
static void crt_ceil(void)   { double a; uint64_t u = MEM64(g_esp+4); memcpy(&a,&u,8); fpush(ceil(a)); CDECLRET(); }
static void crt_ftol(void)   { double a = fpeek(0); fpop(); RET((uint32_t)(int32_t)a); CDECLRET(); }

/* ------------------------------------------------------- KERNEL32 basics */

static void k32_GetModuleHandleA(void) { RET(0x00400000u); STDRET(1); }
static void k32_GetTickCount(void)     { RET((uint32_t)GetTickCount()); STDRET(0); }
static void k32_GetLastError(void)     { RET((uint32_t)GetLastError()); STDRET(0); }
static void k32_Sleep(void)            { Sleep(ARG(0)); RET(0); STDRET(1); }
static void k32_GetModuleFileNameA(void) {
    char p[MAX_PATH];
    snprintf(p, sizeof(p), "%s/Focom.exe", g_gamedir);
    strncpy(ARGP(1,char), p, ARG(2));
    RET((uint32_t)strlen(p)); STDRET(3);
}
static void k32_GetCurrentDirectoryA(void) {
    strncpy(ARGP(1,char), g_gamedir, ARG(0));
    RET((uint32_t)strlen(g_gamedir)); STDRET(2);
}


/* ------------------------------------------- Win32 startup, for real

 * These were stubs returning 0, and a stub that is supposed to FILL IN a
 * caller-provided struct is not a benign stub: the caller reads whatever was
 * on the stack. The CRT startup reads STARTUPINFO.lpReserved2/cbReserved2 to
 * pick up inherited file handles, so garbage there corrupts it -- which is how
 * the entry point ended up running its epilogue with ebp = 0x00400000, a module
 * handle where a frame pointer belonged.
 */
static void k32_GetStartupInfoA(void) {
    uint32_t p = ARG(0);
    if (p) {
        for (uint32_t i = 0; i < 68; i += 4) MEM32(p + i) = 0;
        MEM32(p + 0) = 68;          /* cb */
        MEM16(p + 0x2C) = 0;        /* wShowWindow */
    }
    RET(0); STDRET(1);
}

static void k32_GetVersionExA(void) {
    /* Windows 2000: what a 2000-era game expects, and new enough that no
     * 9x-only path is taken. */
    uint32_t p = ARG(0);
    if (p) {
        MEM32(p + 0x00) = 148;      /* dwOSVersionInfoSize */
        MEM32(p + 0x04) = 5;        /* dwMajorVersion */
        MEM32(p + 0x08) = 0;        /* dwMinorVersion */
        MEM32(p + 0x0C) = 2195;     /* dwBuildNumber */
        MEM32(p + 0x10) = 2;        /* VER_PLATFORM_WIN32_NT */
        for (uint32_t i = 0x14; i < 0x94; i += 4) MEM32(p + i) = 0;
    }
    RET(1); STDRET(1);
}

static void k32_QueryPerformanceFrequency(void) {
    uint32_t p = ARG(0);
    if (p) { MEM32(p) = 1000000u; MEM32(p + 4) = 0; }   /* 1 MHz */
    RET(1); STDRET(1);
}

static void k32_QueryPerformanceCounter(void) {
    uint32_t p = ARG(0);
    if (p) {
        /* Monotonic microseconds, from the host's own performance counter so
         * the game's frame timing is real rather than a fabricated ramp. */
        LARGE_INTEGER c, f;
        QueryPerformanceCounter(&c);
        QueryPerformanceFrequency(&f);
        unsigned long long us = (unsigned long long)
            ((double)c.QuadPart / (double)f.QuadPart * 1000000.0);
        MEM32(p) = (uint32_t)us;
        MEM32(p + 4) = (uint32_t)(us >> 32);
    }
    RET(1); STDRET(1);
}

static void mm_timeGetTime(void)  { RET((uint32_t)GetTickCount()); STDRET(0); }
static void ole_CoInitialize(void) { RET(0); STDRET(1); }   /* S_OK */
static void ole_CoUninitialize(void) { RET(0); STDRET(0); }

/*
 * CoCreateInstance must FAIL, and this is the single most important return
 * value in the file.
 *
 * The generated stub answered 0 -- which is S_OK -- without writing the out
 * pointer. The game's DirectX version probe does:
 *
 *     [esp+0x1c] = 0
 *     CoCreateInstance(CLSID_DirectMusic, ..., &[esp+0x1c])
 *     mov eax, [esp+0x1c]        ; believes it has an object
 *     call [eax]                 ; reads a vtable from NULL
 *
 * so S_OK plus a NULL out pointer is a guaranteed null dereference. Saying
 * "this class is not registered" is both true and what the probe is built to
 * handle -- it is how the answer comes back as DirectX 6 rather than 6.1.
 */
#define REGDB_E_CLASSNOTREG 0x80040154u

static void ole_CoCreateInstance(void) {
    uint32_t ppv = ARG(4);
    uint32_t clsid = ARG(0);
    if (ppv) MEM32(ppv) = 0;
    fprintf(stderr, "[ole] CoCreateInstance({%08X-...}) -> "
                    "REGDB_E_CLASSNOTREG\n", clsid ? MEM32(clsid) : 0);
    RET(REGDB_E_CLASSNOTREG); STDRET(5);
}

/* Critical sections: the game is single-threaded through startup, and a real
 * one cannot be used because the game's CRITICAL_SECTION lives in ITS address
 * space at a size the host's does not match. */
static void k32_cs_init(void)  { RET(0); STDRET(1); }
static void k32_cs_enter(void) { RET(0); STDRET(1); }
static void k32_cs_leave(void) { RET(0); STDRET(1); }
static void k32_cs_del(void)   { RET(0); STDRET(1); }


/* ------------------------------------------ LoadLibrary / GetProcAddress

 * The graphics stack is not in the import table: DDRAW.DLL, SMUSH.DLL,
 * FEELIT.DLL and DINPUT.DLL all arrive through LoadLibrary, and the entry
 * points through GetProcAddress. So this pair is the interception point for the
 * whole renderer, and what it answers decides which path the game takes.
 *
 * Returning 1 from both -- which is what the generated "did it work" stub did --
 * is the worst answer: the game believes it has a module and a function
 * pointer, calls through 1, and faults. Returning a real handle but a NULL
 * proc is the honest one: the game's own "this DLL is present but does not
 * export what I need" path runs, and RE3D has three renderers to fall back
 * through (Direct3D hardware, Direct3D RGB, DirectDraw memory-lock).
 */
#define FAKE_MODULE_BASE 0x7F000000u
static uint32_t g_fake_mod = FAKE_MODULE_BASE;

static void k32_LoadLibraryA(void) {
    const char* n = ARG(0) ? (const char*)(uintptr_t)ADDR(ARG(0)) : "(null)";
    g_fake_mod += 0x10000;
    fprintf(stderr, "[dll] LoadLibraryA(\"%s\") -> 0x%08X (stub module)\n",
            n, g_fake_mod);
    RET(g_fake_mod); STDRET(1);
}

uint32_t ddraw_proc(const char* name);      /* ddraw_shims.c */

static void k32_GetProcAddress(void) {
    const char* n = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "(ordinal)";
    uint32_t va = ddraw_proc(n);
    if (va) {
        fprintf(stderr, "[dll] GetProcAddress(\"%s\") -> 0x%08X\n", n, va);
        RET(va); STDRET(2);
        return;
    }
    fprintf(stderr, "[dll] GetProcAddress(\"%s\") -> NULL (not implemented)\n", n);
    RET(0); STDRET(2);
}

static void k32_FreeLibrary(void) { RET(1); STDRET(1); }

/* -------------------------------------------------------------- registry */

const struct { const char* name; import_fn_t fn; } g_crt_shims[] = {
    { "MSVCRT.dll!malloc",      crt_malloc },
    { "MSVCRT.dll!free",        crt_free },
    { "MSVCRT.dll!calloc",      crt_calloc },
    { "MSVCRT.dll!realloc",     crt_realloc },
    { "MSVCRT.dll!??2@YAPAXI@Z", crt_new },
    { "MSVCRT.dll!memcpy",      crt_memcpy },
    { "MSVCRT.dll!memmove",     crt_memmove },
    { "MSVCRT.dll!memset",      crt_memset },
    { "MSVCRT.dll!memcmp",      crt_memcmp },
    { "MSVCRT.dll!memchr",      crt_memchr },
    { "MSVCRT.dll!strlen",      crt_strlen },
    { "MSVCRT.dll!strcmp",      crt_strcmp },
    { "MSVCRT.dll!strncmp",     crt_strncmp },
    { "MSVCRT.dll!_stricmp",    crt_stricmp },
    { "MSVCRT.dll!_strnicmp",   crt_strnicmp },
    { "MSVCRT.dll!strcpy",      crt_strcpy },
    { "MSVCRT.dll!strncpy",     crt_strncpy },
    { "MSVCRT.dll!strcat",      crt_strcat },
    { "MSVCRT.dll!strchr",      crt_strchr },
    { "MSVCRT.dll!strrchr",     crt_strrchr },
    { "MSVCRT.dll!strstr",      crt_strstr },
    { "MSVCRT.dll!atoi",        crt_atoi },
    { "MSVCRT.dll!atof",        crt_atof },
    { "MSVCRT.dll!toupper",     crt_toupper },
    { "MSVCRT.dll!tolower",     crt_tolower },
    { "MSVCRT.dll!isalpha",     crt_isalpha },
    { "MSVCRT.dll!isdigit",     crt_isdigit },
    { "MSVCRT.dll!isalnum",     crt_isalnum },
    { "MSVCRT.dll!isspace",     crt_isspace },
    { "MSVCRT.dll!isupper",     crt_isupper },
    { "MSVCRT.dll!islower",     crt_islower },
    { "MSVCRT.dll!isprint",     crt_isprint },
    { "MSVCRT.dll!isxdigit",    crt_isxdigit },
    { "MSVCRT.dll!sprintf",     crt_sprintf },
    { "MSVCRT.dll!vsprintf",    crt_vsprintf },
    { "MSVCRT.dll!fprintf",     crt_fprintf },
    { "MSVCRT.dll!fopen",       crt_fopen },
    { "MSVCRT.dll!fclose",      crt_fclose },
    { "MSVCRT.dll!fread",       crt_fread },
    { "MSVCRT.dll!fwrite",      crt_fwrite },
    { "MSVCRT.dll!fseek",       crt_fseek },
    { "MSVCRT.dll!ftell",       crt_ftell },
    { "MSVCRT.dll!fgets",       crt_fgets },
    { "MSVCRT.dll!_initterm",   crt_initterm },
    { "MSVCRT.dll!__p__fmode",  crt_p_fmode },
    { "MSVCRT.dll!__p__commode", crt_p_commode },
    { "MSVCRT.dll!__getmainargs", crt_getmainargs },
    { "MSVCRT.dll!exit",        crt_exit },
    { "MSVCRT.dll!_exit",       crt_exit },
    { "MSVCRT.dll!__set_app_type", crt_noop0 },
    { "MSVCRT.dll!__setusermatherr", crt_noop0 },
    { "MSVCRT.dll!_controlfp",  crt_noop0 },
    { "MSVCRT.dll!_onexit",     crt_noop0 },
    { "MSVCRT.dll!__dllonexit", crt_noop0 },
    { "MSVCRT.dll!_XcptFilter", crt_noop0 },
    { "MSVCRT.dll!_CIpow",      crt_CIpow },
    { "MSVCRT.dll!_CIfmod",     crt_CIfmod },
    { "MSVCRT.dll!_CIacos",     crt_CIacos },
    { "MSVCRT.dll!_CIasin",     crt_CIasin },
    { "MSVCRT.dll!floor",       crt_floor },
    { "MSVCRT.dll!ceil",        crt_ceil },
    { "MSVCRT.dll!_ftol",       crt_ftol },
    { "KERNEL32.dll!GetModuleHandleA",     k32_GetModuleHandleA },
    { "KERNEL32.dll!GetModuleFileNameA",   k32_GetModuleFileNameA },
    { "KERNEL32.dll!GetCurrentDirectoryA", k32_GetCurrentDirectoryA },
    { "KERNEL32.dll!GetTickCount",         k32_GetTickCount },
    { "KERNEL32.dll!GetLastError",         k32_GetLastError },
    { "KERNEL32.dll!Sleep",                k32_Sleep },
    { "KERNEL32.dll!LoadLibraryA",         k32_LoadLibraryA },
    { "KERNEL32.dll!GetProcAddress",       k32_GetProcAddress },
    { "KERNEL32.dll!FreeLibrary",          k32_FreeLibrary },
    { "KERNEL32.dll!GetStartupInfoA",      k32_GetStartupInfoA },
    { "KERNEL32.dll!GetVersionExA",        k32_GetVersionExA },
    { "KERNEL32.dll!QueryPerformanceFrequency", k32_QueryPerformanceFrequency },
    { "KERNEL32.dll!QueryPerformanceCounter",   k32_QueryPerformanceCounter },
    { "KERNEL32.dll!InitializeCriticalSection", k32_cs_init },
    { "KERNEL32.dll!EnterCriticalSection",      k32_cs_enter },
    { "KERNEL32.dll!LeaveCriticalSection",      k32_cs_leave },
    { "KERNEL32.dll!DeleteCriticalSection",     k32_cs_del },
    { "WINMM.dll!timeGetTime",             mm_timeGetTime },
    { "ole32.dll!CoInitialize",            ole_CoInitialize },
    { "ole32.dll!CoUninitialize",          ole_CoUninitialize },
    { "ole32.dll!CoCreateInstance",        ole_CoCreateInstance },
};
const unsigned g_crt_shim_count = sizeof(g_crt_shims) / sizeof(g_crt_shims[0]);
