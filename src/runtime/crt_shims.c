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

/* --trace turns on the per-call shim log. Off by default: the config parse
 * alone prints a line per token, which buries the rare events that matter. */
int g_shim_trace = 0;
/* --waitscale N: stretch every finite Win32 wait. See k32_WaitForSingleObject. */
unsigned g_wait_scale = 1;

/* ------------------------------------------------------------- allocator */

/*
 * The target heap: size-binned free lists over one flat region.
 *
 * This was a bump allocator that never reused anything, with a note saying to
 * swap it for a real one when a run actually ran out rather than before. A run
 * ran out: the moment the RE3D renderer got as far as building its light and
 * texture managers, it went through 1,069,123,488 bytes in 641,500
 * allocations -- the whole gigabyte -- because the game frees constantly
 * (operator delete at 0x0056EAD0 forwards to MSVCRT free) and nothing came
 * back. crt_alloc then returned 0 and the next memset went through a null
 * pointer, which presents as a fault inside msvcrt.dll with no connection to
 * the cause.
 *
 * Binned, not first-fit-with-coalescing, because a linear walk over 600,000
 * blocks per allocation is its own kind of hang. Every size up to 4 KB gets an
 * exact-size LIFO list, so alloc and free are both a handful of instructions;
 * anything larger goes on one list searched first-fit, which is fine because
 * large blocks are rare.
 *
 * The 16-byte header keeps the payload 16-byte aligned, which the STL shims
 * depend on: MSVC's string buffer carries its reference count in the byte
 * before the data, so a live _Ptr always has (p & 15) == 1 and s_ptr_ok reads
 * that as "this is a string buffer and not an integer that happens to land in
 * the heap".
 *
 * ponytail: no coalescing, so a run that allocates a million 32-byte blocks,
 * frees them all and then wants one big one still fails. Add coalescing (a
 * footer, or a walk on exhaustion) if that shape ever shows up; the exact-size
 * reuse is what the game's pattern actually needs.
 */
#define HEAP_BASE 0x10000000u
#define HEAP_SIZE 0x40000000u   /* 1 GB -- see FOCOM_HEAP_SIZE */

#define HEAP_HDR   16u          /* bytes before the payload */
#define HEAP_GRAIN 16u
#define HEAP_BINS  256u         /* 16 .. 4096 bytes, in 16-byte steps */
#define BIN_LARGE  0xFFFFFFFFu
#define HEAP_MAGIC 0x48454150u  /* "HEAP" */

/* header, relative to the payload pointer p */
#define H_SIZE(p)  MEM32((p) - 16)      /* payload bytes, rounded to GRAIN */
#define H_BIN(p)   MEM32((p) - 12)
#define H_NEXT(p)  MEM32((p) - 8)       /* free-list link, while free */
#define H_MAGIC(p) MEM32((p) - 4)

static uint32_t heap_next = HEAP_BASE + HEAP_HDR;
static uint32_t heap_peak = 0;
static uint32_t heap_bin[HEAP_BINS];
static uint32_t heap_large;
static unsigned heap_allocs, heap_reused, heap_frees;

static uint32_t heap_take(uint32_t n16, uint32_t bin) {
    uint32_t p;
    if (bin != BIN_LARGE) {
        p = heap_bin[bin];
        if (!p) return 0;
        heap_bin[bin] = H_NEXT(p);
        heap_reused++;
        return p;
    }
    /* first fit on the large list, keeping the predecessor to unlink */
    uint32_t prev = 0;
    for (p = heap_large; p; prev = p, p = H_NEXT(p)) {
        if (H_SIZE(p) < n16) continue;
        if (prev) H_NEXT(prev) = H_NEXT(p); else heap_large = H_NEXT(p);
        heap_reused++;
        return p;
    }
    return 0;
}

uint32_t crt_alloc(uint32_t n) {
    uint32_t n16 = (n + (HEAP_GRAIN - 1)) & ~(HEAP_GRAIN - 1);
    if (!n16) n16 = HEAP_GRAIN;
    uint32_t bin = (n16 <= HEAP_BINS * HEAP_GRAIN) ? (n16 / HEAP_GRAIN) - 1
                                                   : BIN_LARGE;

    uint32_t p = heap_take(n16, bin);
    if (p) { H_MAGIC(p) = HEAP_MAGIC; heap_allocs++; return p; }

    p = heap_next + HEAP_HDR;
    if (p + n16 > HEAP_BASE + HEAP_SIZE) {
        fprintf(stderr, "[crt] heap exhausted: %u bytes, %u allocations,"
                        " %u frees, %u reused\n",
                heap_peak, heap_allocs, heap_frees, heap_reused);
        return 0;
    }
    heap_next = p + n16;
    heap_peak = heap_next - HEAP_BASE;
    H_SIZE(p) = n16;
    H_BIN(p) = bin;
    H_MAGIC(p) = HEAP_MAGIC;
    heap_allocs++;
    return p;
}

/*
 * Release a block. A pointer that is not one of ours -- a stack address, an
 * interior pointer, something the game never got from here -- is ignored
 * rather than trusted, because the alternative is corrupting a free list and
 * failing somewhere unrelated.
 */
void crt_release(uint32_t p) {
    if (p < HEAP_BASE + HEAP_HDR || p >= heap_next) return;
    if ((p & (HEAP_GRAIN - 1)) != 0) return;
    if (H_MAGIC(p) != HEAP_MAGIC) return;
    H_MAGIC(p) = 0;                       /* so a double free is a no-op */
    heap_frees++;
    uint32_t bin = H_BIN(p);
    if (bin == BIN_LARGE) { H_NEXT(p) = heap_large; heap_large = p; }
    else if (bin < HEAP_BINS) { H_NEXT(p) = heap_bin[bin]; heap_bin[bin] = p; }
}

/* The size crt_alloc recorded for a block. */
uint32_t crt_size_of(uint32_t ptr) {
    return ptr ? H_SIZE(ptr) : 0;
}

uint32_t crt_heap_end(void) { return heap_next; }

void crt_heap_stats(void) {
    printf("  heap: %u allocations (%u reused), %u frees,"
           " %u bytes high-water\n",
           heap_allocs, heap_reused, heap_frees, heap_peak);
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
static void crt_free(void)    { crt_release(ARG(0)); RET(0); CDECLRET(); }
static void crt_calloc(void)  {
    uint32_t n = ARG(0) * ARG(1), p = crt_alloc(n);
    if (p) memset((void*)(uintptr_t)ADDR(p), 0, n);
    RET(p); CDECLRET();
}
static void crt_realloc(void) {
    uint32_t old = ARG(0), n = ARG(1);
    if (!old) { RET(crt_alloc(n)); CDECLRET(); return; }
    uint32_t osz = crt_size_of(old);
    if (n <= osz) { RET(old); CDECLRET(); return; }   /* already big enough */
    uint32_t p = crt_alloc(n);
    if (p) {
        memcpy((void*)(uintptr_t)ADDR(p), (void*)(uintptr_t)ADDR(old), osz);
        crt_release(old);
    }
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
    if (!f)
        fprintf(stderr, "[crt] fopen(\"%s\") failed\n", hp ? hp : "(null)");
    else if (g_shim_trace)
        fprintf(stderr, "[crt] fopen(\"%s\") ok\n", hp);
    RET(f2i(f)); CDECLRET();
}
static void crt_fclose(void) { FILE* f = i2f(ARG(0)); RET(f ? (uint32_t)fclose(f) : 0xFFFFFFFFu); CDECLRET(); }
/*
 * void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
 *
 * Four OUT buffers and no return value, which is exactly the shape that a
 * do-nothing stub gets wrong invisibly: the game reads back whatever was on its
 * stack and uses it as a pointer. It faulted reading 0x6F636572 -- the ASCII
 * "reco" of a nearby string literal -- for precisely that reason. Any of the
 * four may be NULL and must then be skipped.
 */
static void crt_splitpath(void) {
    char path[MAX_PATH * 2];
    char drv[8], dir[MAX_PATH], fn[MAX_PATH], ex[MAX_PATH];
    const char* src = (const char*)(uintptr_t)ADDR(ARG(0));
    snprintf(path, sizeof(path), "%s", src ? src : "");
    _splitpath(path, drv, dir, fn, ex);
    const char* part[4] = { drv, dir, fn, ex };
    for (int k = 0; k < 4; k++)
        if (ARG(k + 1)) strcpy((char*)(uintptr_t)ADDR(ARG(k + 1)), part[k]);
    RET(0); CDECLRET();
}
/*
 * char* _fullpath(char* abs, const char* rel, size_t max)
 *
 * WinMain calls this to turn its own relative data paths into absolute ones and
 * then opens what comes back, so it has to resolve against g_gamedir exactly
 * the way host_path() does -- resolving against the host cwd would hand the
 * game a path its later fopen cannot find. Returning 0 (the stub's behaviour)
 * makes the game give up and exit(0) before it ever opens the .rpk.
 */
static void crt_fullpath(void) {
    char rel[MAX_PATH * 2], full[MAX_PATH * 2];
    const char* r = host_path(ARG(1), rel, sizeof(rel));
    if (!r || !_fullpath(full, r, sizeof(full))) { RET(0); CDECLRET(); return; }
    uint32_t out = ARG(0), max = ARG(2);
    if (!out) { max = (uint32_t)strlen(full) + 1; out = crt_alloc(max); }
    if (!out || !max) { RET(0); CDECLRET(); return; }
    snprintf((char*)(uintptr_t)ADDR(out), max, "%s", full);
    RET(out); CDECLRET();
}
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
    fprintf(stderr, "[crt] exit(%u) called by lifted 0x%08X\n", ARG(0), g_cur_func);
    recomp_dump_trace("exit");
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

/* shims_impl.c owns the machine lock. Every shim that really blocks has to
 * release it, or the thread it is waiting for can never run -- there is no
 * preemption, the switch points ARE the blocking calls. */
void mach_enter(void);
void mach_leave(void);
void mach_yield(uint32_t sleep_ms);
/*
 * Read every argument BEFORE this and write the result AFTER it. ARG(n) reads
 * the simulated stack through g_esp, and between mach_leave() and mach_enter()
 * the machine belongs to another thread -- so `BLOCKING(Sleep(ARG(0)))` samples
 * g_esp while the other thread is using it. That is how a worker's `edi` -- the
 * `this` it had just loaded -- came back as 0 across a Sleep(0).
 */
#define BLOCKING(expr) do { mach_leave(); (expr); mach_enter(); } while (0)

/* shims_impl.c owns the handle table; a host HANDLE is 64 bits. */
uint32_t h2i(HANDLE h);
HANDLE   i2h(uint32_t i);

/* ----------------------------------------------------------- USER32, part 1

 * MessageBoxA first, because the game reports every startup failure through it
 * ("Failed to create file system registry object!", "Initialization error")
 * and a stub returning 0 threw that text away -- the most useful diagnostic in
 * the binary was being discarded on the way out.
 *
 * It prints rather than opening a modal box: a dialog in the middle of bring-up
 * blocks the run and there is nobody to click it.
 */
static void u32_MessageBoxA(void) {
    const char* text = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "";
    const char* cap  = ARG(2) ? (const char*)(uintptr_t)ADDR(ARG(2)) : "";
    fprintf(stderr, "[msgbox] %s: %s\n", cap, text);
    RET(1);                       /* IDOK */
    STDRET(4);
}
static void u32_GetSystemMetrics(void) { RET((uint32_t)GetSystemMetrics((int)ARG(0))); STDRET(1); }
static void u32_GetDesktopWindow(void) { RET(h2i(GetDesktopWindow())); STDRET(0); }
static void u32_CharNextA(void) {
    const char* p = ARG(0) ? (const char*)(uintptr_t)ADDR(ARG(0)) : NULL;
    RET(p && *p ? ARG(0) + 1 : ARG(0)); STDRET(1);
}
static void u32_CharPrevA(void) {
    RET(ARG(1) > ARG(0) ? ARG(1) - 1 : ARG(0)); STDRET(2);
}
static void u32_ShowCursor(void) { RET((uint32_t)ShowCursor((BOOL)ARG(0))); STDRET(1); }
static void u32_LoadCursorA(void) { RET(h2i(LoadCursorA(NULL, (LPCSTR)(uintptr_t)ARG(1)))); STDRET(2); }
static void u32_SetCursor(void) { RET(h2i(SetCursor((HCURSOR)i2h(ARG(0))))); STDRET(1); }
static void u32_GetKeyState(void) { RET((uint32_t)(int32_t)GetKeyState((int)ARG(0))); STDRET(1); }
static void u32_GetStockObject(void) { RET(h2i(GetStockObject((int)ARG(0)))); STDRET(1); }

/* ------------------------------------------------- MSVCRT C++ runtime

 * RTTI, bsearch and the exception entry points. These are the last group of
 * stubs that fail silently rather than loudly: bsearch returning 0 means "not
 * found" for every lookup the game makes, and __RTDynamicCast returning 0 means
 * every dynamic_cast fails, so the game takes its not-this-type branch and
 * leaves objects unconstructed. Both surface much later as a string with no
 * storage.
 *
 * The MSVC 6 RTTI layout, all in the mapped image and all 32-bit:
 *
 *   vfptr[-1] -> CompleteObjectLocator
 *      +0x00 signature   +0x04 offset (of the vfptr in the complete object)
 *      +0x08 cdOffset    +0x0C TypeDescriptor*   +0x10 ClassHierarchyDescriptor*
 *   ClassHierarchyDescriptor
 *      +0x00 signature   +0x04 attributes
 *      +0x08 numBaseClasses                      +0x0C BaseClassDescriptor**
 *   BaseClassDescriptor
 *      +0x00 TypeDescriptor*     +0x04 numContainedBases
 *      +0x08 mdisp  +0x0C pdisp  +0x10 vdisp     +0x14 attributes
 *   TypeDescriptor
 *      +0x00 vfptr  +0x04 spare  +0x08 decorated name, NUL-terminated
 */
#define COL_OFFSET(c)      MEM32((c) + 0x04)
#define COL_TYPEDESC(c)    MEM32((c) + 0x0C)
#define COL_HIERARCHY(c)   MEM32((c) + 0x10)
#define CHD_NUMBASES(h)    MEM32((h) + 0x08)
#define CHD_BASEARRAY(h)   MEM32((h) + 0x0C)
#define BCD_TYPEDESC(b)    MEM32((b) + 0x00)
#define BCD_MDISP(b)       MEM32((b) + 0x08)
#define BCD_PDISP(b)       MEM32((b) + 0x0C)
#define BCD_VDISP(b)       MEM32((b) + 0x10)
#define TD_NAME(t)         ((const char*)(uintptr_t)ADDR((t) + 0x08))

/* __thiscall receives `this` in ecx, same as stl_shims.c. */
#define THIS (g_ecx)

static void crt_purecall(void) {
    fprintf(stderr, "[cxx] pure virtual call from 0x%08X\n", g_cur_func);
    recomp_dump_trace("purecall");
    abort();
}
static void crt_callnewh(void) { RET(0); CDECLRET(); }          /* no new_handler */
static void crt_terminate(void) {
    fprintf(stderr, "[cxx] std::terminate from 0x%08X\n", g_cur_func);
    recomp_dump_trace("terminate");
    abort();
}
static void crt_typeinfo_dtor(void) { RET(THIS); STDRET(0); }   /* nothing owned */
static void crt_typeinfo_eq(void) {
    /* type_info::operator==: within one image the descriptors are unique, so
     * pointer equality is enough, but the names settle a tie across a
     * duplicated descriptor. */
    uint32_t a = THIS, b = ARG(0);
    int eq = (a == b);
    if (!eq && a && b) eq = !strcmp((const char*)(uintptr_t)ADDR(a + 0x08),
                                    (const char*)(uintptr_t)ADDR(b + 0x08));
    RET(eq ? 1 : 0); STDRET(1);
}

/* The locator sitting one slot before an object's vftable. */
static uint32_t rtti_locator(uint32_t obj, int32_t vfdelta) {
    if (!obj) return 0;
    uint32_t vfptr = MEM32(obj + (uint32_t)vfdelta);
    if (vfptr < 0x00400000u) return 0;
    return MEM32(vfptr - 4);
}

static void crt_RTtypeid(void) {
    uint32_t col = rtti_locator(ARG(0), 0);
    RET(col ? COL_TYPEDESC(col) : 0); CDECLRET();
}

static void crt_RTDynamicCast(void) {
    uint32_t obj = ARG(0), target = ARG(3);
    int32_t vfdelta = (int32_t)ARG(1);
    uint32_t col = rtti_locator(obj, vfdelta);
    if (!col || !target) { RET(0); CDECLRET(); return; }

    /* The complete object starts `offset` bytes before the subobject holding
     * the vfptr we just read. */
    uint32_t complete = obj + (uint32_t)vfdelta - COL_OFFSET(col);
    uint32_t hier = COL_HIERARCHY(col);
    uint32_t n = hier ? CHD_NUMBASES(hier) : 0;
    uint32_t arr = hier ? CHD_BASEARRAY(hier) : 0;
    const char* want = TD_NAME(target);

    for (uint32_t i = 0; i < n && arr; i++) {
        uint32_t bcd = MEM32(arr + i * 4);
        if (!bcd) continue;
        uint32_t td = BCD_TYPEDESC(bcd);
        if (td != target && (!td || strcmp(TD_NAME(td), want))) continue;
        /* Non-virtual base: mdisp is the whole adjustment. A virtual one adds
         * the displacement found through pdisp/vdisp. */
        uint32_t base = complete + BCD_MDISP(bcd);
        if ((int32_t)BCD_PDISP(bcd) >= 0) {
            uint32_t vbtbl = MEM32(complete + BCD_PDISP(bcd));
            base = complete + BCD_PDISP(bcd)
                 + (int32_t)MEM32(vbtbl + BCD_VDISP(bcd)) + BCD_MDISP(bcd);
        }
        RET(base); CDECLRET();
        return;
    }
    RET(0); CDECLRET();          /* a failed dynamic_cast is legitimate */
}

static void crt_CxxThrowException(void) {
    /*
     * ponytail: report and stop. Honouring a throw means running the MSVC
     * unwinder over the simulated stack and calling each frame's funclets,
     * which is a project of its own; nothing in the startup path is supposed
     * to throw, so a throw here is a finding, not a control path. Implement
     * unwinding if the game turns out to use exceptions for flow.
     */
    uint32_t info = ARG(1);
    const char* name = "(unknown)";
    if (info) {
        uint32_t cat = MEM32(info + 0x0C);            /* pCatchableTypeArray */
        if (cat && MEM32(cat)) {
            uint32_t ct = MEM32(cat + 4);             /* first CatchableType */
            uint32_t td = ct ? MEM32(ct + 4) : 0;     /* its TypeDescriptor */
            if (td) name = TD_NAME(td);
        }
    }
    fprintf(stderr, "[cxx] throw %s from 0x%08X (no unwinder)\n", name, g_cur_func);
    recomp_dump_trace("throw");
    abort();
}
static void crt_frame_handler(void) {
    fprintf(stderr, "[cxx] __CxxFrameHandler reached from 0x%08X\n", g_cur_func);
    abort();
}
static void crt_except_handler3(void) {
    fprintf(stderr, "[cxx] _except_handler3 reached from 0x%08X\n", g_cur_func);
    abort();
}

/*
 * bsearch(key, base, num, width, compare). The comparison function is lifted
 * code, so each probe re-enters the machine. Returning 0 unconditionally -- the
 * stub's behaviour -- makes every table lookup in the game miss.
 */
static void crt_bsearch(void) {
    uint32_t key = ARG(0), base = ARG(1), num = ARG(2), width = ARG(3);
    recomp_func_t cmp = recomp_lookup(ARG(4));
    if (!cmp || !width) {
        fprintf(stderr, "[crt] bsearch: comparator 0x%08X is not lifted\n", ARG(4));
        RET(0); CDECLRET(); return;
    }
    uint32_t lo = 0, hi = num;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t elem = base + mid * width;
        PUSH32(g_esp, elem);
        PUSH32(g_esp, key);
        PUSH32(g_esp, RECOMP_RETADDR);
        uint32_t save = g_cur_func;
        cmp();
        g_cur_func = save;
        g_esp += 8;                       /* cdecl: the caller pops */
        int32_t r = (int32_t)g_eax;
        if (r == 0) { RET(elem); CDECLRET(); return; }
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    RET(0); CDECLRET();
}

static void crt_mktime(void) {
    /* struct tm is nine ints in both ABIs. */
    struct tm t;
    if (!ARG(0)) { RET(0xFFFFFFFFu); CDECLRET(); return; }
    uint32_t o = ARG(0);
    t.tm_sec = (int)MEM32(o + 0);  t.tm_min  = (int)MEM32(o + 4);
    t.tm_hour = (int)MEM32(o + 8); t.tm_mday = (int)MEM32(o + 12);
    t.tm_mon = (int)MEM32(o + 16); t.tm_year = (int)MEM32(o + 20);
    t.tm_wday = (int)MEM32(o + 24); t.tm_yday = (int)MEM32(o + 28);
    t.tm_isdst = (int)MEM32(o + 32);
    time_t r = mktime(&t);
    MEM32(o + 24) = (uint32_t)t.tm_wday;
    MEM32(o + 28) = (uint32_t)t.tm_yday;
    RET((uint32_t)(int32_t)r); CDECLRET();
}


/* ------------------------------------------- Miles, WINMM and ShellExecute

 * Silent, but honest. These are the last of the imports whose generated stub
 * returns 0 without filling anything in, and 0 is the wrong answer for most of
 * them: AIL_startup returning 0 reads as "the sound system failed to start",
 * which is not the same as "there is no sound card".
 *
 * ponytail: no audio is produced. The Miles API is answered the way a working
 * driver with no output device would answer it -- startup succeeds, a sample
 * handle is a real (if inert) allocation so the game can set properties on it,
 * and every sample reports itself finished. Playing sound means either loading
 * the real mss32.dll (it is on the disc, and a 32-bit host could) or decoding
 * the formats; neither is on the way to a first frame.
 *
 * Miles exports are decorated `@N`, so the purge counts come from the names and
 * are not guesses.
 */
#define SMP_DONE 2

static void ail_startup(void)        { RET(1); STDRET(0); }
static void ail_shutdown(void)       { RET(0); STDRET(0); }
static void ail_last_error(void)     { RET(0); STDRET(0); }   /* NULL = no error */
static void ail_set_preference(void) { (void)ARG(0); RET(0); STDRET(2); }
static void ail_waveOutOpen(void) {
    /* (driver**, wave_out**, index, format): hand back one inert driver. */
    if (ARG(0)) MEM32(ARG(0)) = crt_alloc(64);
    if (ARG(1)) MEM32(ARG(1)) = 0;
    RET(0); STDRET(4);                                        /* M_OK */
}
static void ail_waveOutClose(void)   { (void)ARG(0); RET(0); STDRET(1); }
static void ail_digital_handle_release(void) { RET(0); STDRET(1); }
static void ail_digital_handle_reacquire(void) { RET(0); STDRET(1); }
static void ail_get_DirectSound_info(void) {
    /* (sample, &dsound, &dsbuffer) -- both out pointers, and a stub that left
     * them alone had the game read stack garbage as COM interfaces. */
    if (ARG(1)) MEM32(ARG(1)) = 0;
    if (ARG(2)) MEM32(ARG(2)) = 0;
    RET(0); STDRET(3);
}
static void ail_set_DirectSound_HWND(void) { RET(0); STDRET(2); }

/* A sample handle has to be non-NULL and writable: the game sets a file, a
 * volume, a pan and a loop count on it before playing. 128 bytes of target heap
 * is enough for it to scribble on. */
static void ail_allocate_sample_handle(void) { RET(crt_alloc(128)); STDRET(1); }
static void ail_release_sample_handle(void)  { (void)ARG(0); RET(0); STDRET(1); }
static void ail_init_sample(void)            { RET(0); STDRET(1); }
static void ail_set_sample_file(void)        { RET(1); STDRET(3); }   /* accepted */
static void ail_set_named_sample_file(void)  { RET(1); STDRET(5); }
static void ail_start_sample(void)           { RET(0); STDRET(1); }
static void ail_end_sample(void)             { RET(0); STDRET(1); }
static void ail_sample_status(void)          { RET(SMP_DONE); STDRET(1); }
static void ail_set_sample_volume(void)      { RET(0); STDRET(2); }
static void ail_set_sample_pan(void)         { RET(0); STDRET(2); }
static void ail_set_sample_loop_count(void)  { RET(0); STDRET(2); }
static void ail_set_sample_playback_rate(void) { RET(0); STDRET(2); }

/* ------------------------------------------------------------------ WINMM */

static void mm_timeBeginPeriod(void) { RET(0); STDRET(1); }   /* TIMERR_NOERROR */
static void mm_timeEndPeriod(void)   { RET(0); STDRET(1); }
static void mm_timeGetDevCaps(void) {
    /* TIMECAPS is two UINTs. A stub leaving it alone gave the game a minimum
     * period of whatever was on the stack. */
    if (ARG(0) && ARG(1) >= 8) {
        MEM32(ARG(0)) = 1;                                    /* wPeriodMin */
        MEM32(ARG(0) + 4) = 1000000;                          /* wPeriodMax */
    }
    RET(0); STDRET(2);
}
uint32_t mm_timer_create(uint32_t delay, uint32_t proc, uint32_t user,
                         uint32_t flags);                  /* shims_impl.c */
void mm_timer_kill(uint32_t id);                           /* shims_impl.c */

static void mm_timeSetEvent(void) {
    /* The callback really runs now -- see mm_timer_create. 0 means "could not
     * create the timer", which the game treats as a fatal audio error. */
    RET(mm_timer_create(ARG(0), ARG(2), ARG(3), ARG(4))); STDRET(5);
}
static void mm_timeKillEvent(void) { mm_timer_kill(ARG(0)); RET(0); STDRET(1); }
static void mm_PlaySoundA(void)    { RET(1); STDRET(3); }
static void mm_mciSendStringA(void) {
    /* The game drives CD audio through MCI ("open cdaudio", "play cdaudio
     * from %d to %d"). Report success and clear the reply buffer, which a stub
     * left holding stack contents for the game to parse. */
    if (ARG(1) && ARG(2)) memset((void*)(uintptr_t)ADDR(ARG(1)), 0, ARG(2));
    RET(0); STDRET(4);
}

static void sh_ShellExecuteA(void) { RET(42); STDRET(6); }    /* >32 = success */


/* ------------------------------------------------- KERNEL32 sync and files

 * Focom.ini's first directive is `CheckAppMutex FORCE`, so CreateMutexA is the
 * very first thing the startup script asks for. A stub returning 0 reads as
 * "could not create the mutex", and the game treats that the same as "another
 * instance is already running".
 */
static void k32_CreateMutexA(void) {
    const char* nm = ARG(2) ? (const char*)(uintptr_t)ADDR(ARG(2)) : NULL;
    HANDLE h = CreateMutexA(NULL, (BOOL)ARG(1), nm);
    RET(h ? h2i(h) : 0); STDRET(3);
}
static void k32_ReleaseMutex(void) {
    HANDLE h = i2h(ARG(0));
    RET(h ? (ReleaseMutex(h) ? 1 : 0) : 0); STDRET(1);
}
static void k32_CreateEventA(void) {
    const char* nm = ARG(3) ? (const char*)(uintptr_t)ADDR(ARG(3)) : NULL;
    HANDLE h = CreateEventA(NULL, (BOOL)ARG(1), (BOOL)ARG(2), nm);
    if (g_shim_trace) fprintf(stderr, "[k32] CreateEventA(manual=%u, set=%u, \"%s\") -> h=%u\n", ARG(1), ARG(2), nm ? nm : "", h2i(h));
    RET(h ? h2i(h) : 0); STDRET(4);
}
static void k32_SetEvent(void) {
    HANDLE h = i2h(ARG(0));
    if (g_shim_trace)
        fprintf(stderr, "[k32] SetEvent(h=%u) from 0x%08X\n", ARG(0), g_cur_func);
    RET(h ? (SetEvent(h) ? 1 : 0) : 0); STDRET(1);
}

static void k32_ResetEvent(void) {
    HANDLE h = i2h(ARG(0));
    if (g_shim_trace)
        fprintf(stderr, "[k32] ResetEvent(h=%u) from 0x%08X\n", ARG(0), g_cur_func);
    RET(h ? (ResetEvent(h) ? 1 : 0) : 0); STDRET(1);
}
static void k32_PulseEvent(void) { HANDLE h = i2h(ARG(0)); RET(h ? (PulseEvent(h) ? 1 : 0) : 0); STDRET(1); }
static void k32_WaitForSingleObject(void) {
    HANDLE h = i2h(ARG(0));
    /*
     * ponytail: wall-clock timeouts are stretched by g_wait_scale.
     *
     * Ronin's thread body is `WaitForSingleObject(ev, 100)` and it RETURNS on
     * timeout -- the thread ends if it is not given work within 100 ms. That
     * assumes real concurrency, and this machine runs one thread of lifted code
     * at a time, so wall-clock time passes while a thread waits its turn for
     * the machine. The mission process timed out and was reaped after one
     * frame, which is why attaching a --calltrace "fixed" it.
     *
     * The honest fix is a virtual clock: GetTickCount and friends advancing
     * with progress rather than with the wall. That is the next structural
     * step; scaling the timeout is the measurement that says it is the right
     * one.
     */
    uint32_t r = 0xFFFFFFFFu, ms = ARG(1);      /* sampled before releasing */
    if (ms != INFINITE) {
        uint64_t scaled = (uint64_t)ms * g_wait_scale;
        ms = scaled > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)scaled;
    }
    if (h) BLOCKING(r = (uint32_t)WaitForSingleObject(h, ms));
    static unsigned n;
    if (g_shim_trace && n++ < 12)
        fprintf(stderr, "[k32] WaitForSingleObject(h=%u, %u) -> %u\n", ARG(0), ARG(1), r);
    RET(r); STDRET(2);
}
static void k32_CopyFileA(void) {
    char a[MAX_PATH * 2], b[MAX_PATH * 2], p[MAX_PATH * 2];
    const char* s = host_path(ARG(0), a, sizeof(a));
    snprintf(p, sizeof(p), "%s", s ? s : "");
    const char* dst = host_path(ARG(1), b, sizeof(b));
    RET(s && dst ? (CopyFileA(p, dst, (BOOL)ARG(2)) ? 1 : 0) : 0); STDRET(3);
}
static void k32_MoveFileA(void) {
    char a[MAX_PATH * 2], b[MAX_PATH * 2], p[MAX_PATH * 2];
    const char* s = host_path(ARG(0), a, sizeof(a));
    snprintf(p, sizeof(p), "%s", s ? s : "");
    const char* dst = host_path(ARG(1), b, sizeof(b));
    RET(s && dst ? (MoveFileA(p, dst) ? 1 : 0) : 0); STDRET(2);
}
static void k32_RemoveDirectoryA(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    RET(hp ? (RemoveDirectoryA(hp) ? 1 : 0) : 0); STDRET(1);
}
static void k32_SetFileAttributesA(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    RET(hp ? (SetFileAttributesA(hp, ARG(1)) ? 1 : 0) : 0); STDRET(2);
}
/* FILETIME and SYSTEMTIME have identical 32- and 64-bit layouts. */
static void k32_FileTimeToLocalFileTime(void) {
    FILETIME in, out;
    if (!ARG(0) || !ARG(1)) { RET(0); STDRET(2); return; }
    memcpy(&in, (void*)(uintptr_t)ADDR(ARG(0)), sizeof(in));
    BOOL ok = FileTimeToLocalFileTime(&in, &out);
    memcpy((void*)(uintptr_t)ADDR(ARG(1)), &out, sizeof(out));
    RET(ok ? 1 : 0); STDRET(2);
}
static void k32_LocalFileTimeToFileTime(void) {
    FILETIME in, out;
    if (!ARG(0) || !ARG(1)) { RET(0); STDRET(2); return; }
    memcpy(&in, (void*)(uintptr_t)ADDR(ARG(0)), sizeof(in));
    BOOL ok = LocalFileTimeToFileTime(&in, &out);
    memcpy((void*)(uintptr_t)ADDR(ARG(1)), &out, sizeof(out));
    RET(ok ? 1 : 0); STDRET(2);
}
static void k32_FileTimeToSystemTime(void) {
    FILETIME in; SYSTEMTIME out;
    if (!ARG(0) || !ARG(1)) { RET(0); STDRET(2); return; }
    memcpy(&in, (void*)(uintptr_t)ADDR(ARG(0)), sizeof(in));
    BOOL ok = FileTimeToSystemTime(&in, &out);
    memcpy((void*)(uintptr_t)ADDR(ARG(1)), &out, sizeof(out));
    RET(ok ? 1 : 0); STDRET(2);
}
static void k32_SystemTimeToFileTime(void) {
    SYSTEMTIME in; FILETIME out;
    if (!ARG(0) || !ARG(1)) { RET(0); STDRET(2); return; }
    memcpy(&in, (void*)(uintptr_t)ADDR(ARG(0)), sizeof(in));
    BOOL ok = SystemTimeToFileTime(&in, &out);
    memcpy((void*)(uintptr_t)ADDR(ARG(1)), &out, sizeof(out));
    RET(ok ? 1 : 0); STDRET(2);
}

/* ------------------------------------------------------------- ADVAPI32

 * The game keeps its settings in the registry, under
 *
 *   LucasArts Entertainment Company LLC\Force Commander\v1.0\Settings\Screen
 *   ...\Settings\Game        (UseMipmapping, LODLevel, FogLevel, Brightness)
 *
 * and its own error string for the failure path is "Failed to create file
 * system registry object!". With the whole Reg* family stubbed to return 0 and
 * write nothing, every one of those reads came back as an uninitialised out
 * parameter -- which is how a stack object's member at +0x14 ended up NULL and
 * a virtual call went through it.
 *
 * These pass through to the real registry rather than to an invented store, so
 * the game writes its own defaults on first run and there is nothing to guess.
 * HKEY_LOCAL_MACHINE is redirected to HKEY_CURRENT_USER: the game was written
 * for a single-user Windows 98 and writing under HKLM now needs elevation.
 */
static HKEY reg_key(uint32_t v) {
    switch (v) {
    case 0x80000000u: return HKEY_CLASSES_ROOT;
    case 0x80000001u: return HKEY_CURRENT_USER;
    case 0x80000002u: return HKEY_CURRENT_USER;   /* HKLM, redirected */
    case 0x80000003u: return HKEY_USERS;
    case 0x80000005u: return HKEY_CURRENT_CONFIG;
    default: return (HKEY)i2h(v);
    }
}
static const char* reg_str(uint32_t va) {
    return va ? (const char*)(uintptr_t)ADDR(va) : NULL;
}
static void adv_RegOpenKeyExA(void) {
    HKEY out = NULL;
    LONG r = RegOpenKeyExA(reg_key(ARG(0)), reg_str(ARG(1)), ARG(2),
                           ARG(3) | KEY_READ, &out);
    if (r == ERROR_SUCCESS && ARG(4)) MEM32(ARG(4)) = h2i(out);
    RET((uint32_t)r); STDRET(5);
}
static void adv_RegCreateKeyExA(void) {
    HKEY out = NULL;
    DWORD disp = 0;
    LONG r = RegCreateKeyExA(reg_key(ARG(0)), reg_str(ARG(1)), 0, NULL, ARG(4),
                             ARG(5) | KEY_READ | KEY_WRITE, NULL, &out, &disp);
    if (g_shim_trace)
        fprintf(stderr, "[reg] create %s -> %ld\n", reg_str(ARG(1)) ? reg_str(ARG(1)) : "(null)", r);
    if (r == ERROR_SUCCESS && ARG(7)) MEM32(ARG(7)) = h2i(out);
    if (ARG(8)) MEM32(ARG(8)) = (uint32_t)disp;
    RET((uint32_t)r); STDRET(9);
}
static void adv_RegQueryValueExA(void) {
    DWORD type = 0, cb = ARG(5) ? MEM32(ARG(5)) : 0;
    BYTE* data = ARG(4) ? (BYTE*)(uintptr_t)ADDR(ARG(4)) : NULL;
    LONG r = RegQueryValueExA(reg_key(ARG(0)), reg_str(ARG(1)), NULL,
                              &type, data, ARG(5) ? &cb : NULL);
    if (g_shim_trace)
        fprintf(stderr, "[reg] query %s -> %ld\n", reg_str(ARG(1)) ? reg_str(ARG(1)) : "(null)", r);
    if (ARG(3)) MEM32(ARG(3)) = (uint32_t)type;
    if (ARG(5)) MEM32(ARG(5)) = (uint32_t)cb;
    RET((uint32_t)r); STDRET(6);
}
static void adv_RegSetValueExA(void) {
    const BYTE* data = ARG(4) ? (const BYTE*)(uintptr_t)ADDR(ARG(4)) : NULL;
    LONG r = RegSetValueExA(reg_key(ARG(0)), reg_str(ARG(1)), 0, ARG(3),
                            data, ARG(5));
    RET((uint32_t)r); STDRET(6);
}
static void adv_RegCloseKey(void) {
    HKEY k = reg_key(ARG(0));
    /* Never close a predefined key: the handle is shared process-wide. */
    RET((uint32_t)((ARG(0) & 0x80000000u) ? ERROR_SUCCESS
                                          : (k ? RegCloseKey(k) : ERROR_SUCCESS)));
    STDRET(1);
}

/* Small KERNEL32 leftovers the game touches on the settings path. */
static void k32_GetCurrentThreadId(void)  { RET((uint32_t)GetCurrentThreadId()); STDRET(0); }
static void k32_GetCurrentThread(void)    { RET(0xFFFFFFFEu); STDRET(0); }
static void k32_lstrlenA(void)            { const char* p = reg_str(ARG(0));
                                            RET(p ? (uint32_t)strlen(p) : 0); STDRET(1); }
static void k32_LocalFree(void)           { (void)ARG(0); RET(0); STDRET(1); }
static void k32_OutputDebugStringA(void)  { const char* p = reg_str(ARG(0));
                                            if (p) fprintf(stderr, "[dbg] %s", p);
                                            RET(0); STDRET(1); }
static void k32_GetLocalTime(void) {
    /* 8 WORDs: year, month, dow, day, hour, minute, second, ms. Same layout
     * on both word sizes, so the host struct copies straight over. */
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (ARG(0)) memcpy((void*)(uintptr_t)ADDR(ARG(0)), &st, sizeof(st));
    RET(0); STDRET(1);
}
static void k32_CreateDirectoryA(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    RET(hp ? (CreateDirectoryA(hp, NULL) ? 1 : 0) : 0); STDRET(2);
}
static void k32_DeleteFileA(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    RET(hp ? (DeleteFileA(hp) ? 1 : 0) : 0); STDRET(1);
}
static void k32_SetEndOfFile(void) {
    HANDLE h = i2h(ARG(0));
    RET(h ? (SetEndOfFile(h) ? 1 : 0) : 0); STDRET(1);
}
static void k32_GlobalMemoryStatus(void) {
    MEMORYSTATUS ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    if (!ARG(0)) { RET(0); STDRET(1); return; }
    /* MEMORYSTATUS is eight DWORDs in the 32-bit ABI; the host struct uses
     * SIZE_T, which is 64-bit here, so it has to be copied field by field. */
    uint32_t o = ARG(0);
    MEM32(o + 0x00) = 32;
    MEM32(o + 0x04) = ms.dwMemoryLoad;
    MEM32(o + 0x08) = (uint32_t)(ms.dwTotalPhys      > 0x7FFFFFFF ? 0x7FFFFFFF : ms.dwTotalPhys);
    MEM32(o + 0x0C) = (uint32_t)(ms.dwAvailPhys      > 0x7FFFFFFF ? 0x7FFFFFFF : ms.dwAvailPhys);
    MEM32(o + 0x10) = (uint32_t)(ms.dwTotalPageFile  > 0x7FFFFFFF ? 0x7FFFFFFF : ms.dwTotalPageFile);
    MEM32(o + 0x14) = (uint32_t)(ms.dwAvailPageFile  > 0x7FFFFFFF ? 0x7FFFFFFF : ms.dwAvailPageFile);
    MEM32(o + 0x18) = (uint32_t)(ms.dwTotalVirtual   > 0x7FFFFFFF ? 0x7FFFFFFF : ms.dwTotalVirtual);
    MEM32(o + 0x1C) = (uint32_t)(ms.dwAvailVirtual   > 0x7FFFFFFF ? 0x7FFFFFFF : ms.dwAvailVirtual);
    RET(0); STDRET(1);
}
static void k32_GetSystemInfo(void) {
    /* SYSTEM_INFO carries two pointers, so the 32-bit layout differs from the
     * host's; only the fields the game can act on are filled. */
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (!ARG(0)) { RET(0); STDRET(1); return; }
    uint32_t o = ARG(0);
    memset((void*)(uintptr_t)ADDR(o), 0, 36);
    MEM32(o + 0x00) = si.wProcessorArchitecture;
    MEM32(o + 0x04) = si.dwPageSize;
    MEM32(o + 0x08) = 0x00010000u;                 /* lpMinimumApplicationAddress */
    MEM32(o + 0x0C) = 0x7FFEFFFFu;                 /* lpMaximumApplicationAddress */
    MEM32(o + 0x10) = (uint32_t)si.dwActiveProcessorMask;
    MEM32(o + 0x14) = si.dwNumberOfProcessors;
    MEM32(o + 0x18) = si.dwProcessorType;
    MEM32(o + 0x1C) = si.dwAllocationGranularity;
    MEM32(o + 0x20) = (uint32_t)si.wProcessorLevel | ((uint32_t)si.wProcessorRevision << 16);
    RET(0); STDRET(1);
}

/* --------------------------------------------------- KERNEL32 file access

 * The game reaches these right after startup, scanning for its own data:
 * FindFirstFileA over the install directory, then CreateFileA/ReadFile on what
 * it finds. forcecommand.rpk is 274 MB and every model, texture, animation and
 * mission lives inside it, so nothing appears on screen until these work.
 *
 * All of them go through host_path(), so a relative name resolves under
 * g_gamedir exactly the way fopen does, and through the h2i/i2h table, because
 * a host HANDLE is 64 bits and the lifted code holds 32.
 */
static void k32_CreateFileA(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    HANDLE h = hp ? CreateFileA(hp, ARG(1), ARG(2), NULL, ARG(4), ARG(5), NULL)
                  : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[k32] CreateFileA(\"%s\") failed (%lu)\n",
                hp ? hp : "(null)", GetLastError());
        RET(0xFFFFFFFFu); STDRET(7); return;
    }
    if (g_shim_trace)
        fprintf(stderr, "[k32] CreateFileA(\"%s\") -> h=%u\n", hp, h2i(h));
    RET(h2i(h)); STDRET(7);
}
static void k32_ReadFile(void) {
    DWORD got = 0;
    HANDLE h = i2h(ARG(0));
    BOOL ok = h && ReadFile(h, ARGP(1, char), ARG(2), &got, NULL);
    if (g_shim_trace)
        fprintf(stderr, "[k32] ReadFile(h=%u->%p, %u) -> %d got=%lu\n", ARG(0), (void*)h, ARG(2), ok, got);
    if (ARG(3)) MEM32(ARG(3)) = (uint32_t)got;
    RET(ok ? 1 : 0); STDRET(5);
}
static void k32_WriteFile(void) {
    DWORD put = 0;
    HANDLE h = i2h(ARG(0));
    BOOL ok = h && WriteFile(h, ARGP(1, char), ARG(2), &put, NULL);
    if (ARG(3)) MEM32(ARG(3)) = (uint32_t)put;
    RET(ok ? 1 : 0); STDRET(5);
}
static void k32_CloseHandle(void) {
    HANDLE h = i2h(ARG(0));
    RET(h ? (CloseHandle(h) ? 1 : 0) : 0); STDRET(1);
}
static void k32_SetFilePointer(void) {
    /* The high dword is IN/OUT and its pointer may be NULL. A failed seek has
     * to come back as 0xFFFFFFFF, not 0, or the game reads it as "now at the
     * start of the file" and carries on against the wrong offset. */
    HANDLE h = i2h(ARG(0));
    LONG hi = ARG(2) ? (LONG)MEM32(ARG(2)) : 0;
    DWORD lo = h ? SetFilePointer(h, (LONG)ARG(1), ARG(2) ? &hi : NULL, ARG(3))
                 : INVALID_SET_FILE_POINTER;
    if (ARG(2)) MEM32(ARG(2)) = (uint32_t)hi;
    RET((uint32_t)lo); STDRET(4);
}
static void k32_GetFileSize(void) {
    DWORD hi = 0;
    HANDLE h = i2h(ARG(0));
    DWORD lo = h ? GetFileSize(h, &hi) : INVALID_FILE_SIZE;
    if (ARG(1)) MEM32(ARG(1)) = (uint32_t)hi;
    RET((uint32_t)lo); STDRET(2);
}
static void k32_GetFileAttributesA(void) {
    char p[MAX_PATH * 2];
    const char* hp = host_path(ARG(0), p, sizeof(p));
    RET(hp ? (uint32_t)GetFileAttributesA(hp) : 0xFFFFFFFFu); STDRET(1);
}
/*
 * WIN32_FIND_DATAA has no pointer members, so its 32- and 64-bit layouts are
 * identical (320 bytes) and the host struct copies straight into target memory.
 * That is the only reason this is a memcpy rather than the field-by-field
 * marshal PAINTSTRUCT needs.
 */
static void find_data_out(uint32_t va, const WIN32_FIND_DATAA* fd) {
    if (va) memcpy((void*)(uintptr_t)ADDR(va), fd, sizeof(*fd));
}
static void k32_FindFirstFileA(void) {
    char p[MAX_PATH * 2];
    /* Zeroed, because the whole 320 bytes get copied into target memory and
     * Windows only guarantees the fields it fills. Left uninitialised, the tail
     * of cFileName carried host stack contents across -- including 64-bit host
     * pointers, whose high half then read back as a _Ptr of 0x00007FF7 when the
     * game reused that stack region for a string. Nothing from the host address
     * space may ever be visible to the target. */
    WIN32_FIND_DATAA fd;
    memset(&fd, 0, sizeof(fd));
    const char* hp = host_path(ARG(0), p, sizeof(p));
    HANDLE h = hp ? FindFirstFileA(hp, &fd) : INVALID_HANDLE_VALUE;
    /* A failed enumeration is reported the way a failed open is: always.
     * An empty directory and a missing one look identical to the caller,
     * and a front end that enumerates a directory it needs and finds
     * nothing waits forever without saying so. */
    if (g_shim_trace || h == INVALID_HANDLE_VALUE)
        fprintf(stderr, "[k32] FindFirstFileA(\"%s\") -> %s\n", hp ? hp : "(null)",
            h == INVALID_HANDLE_VALUE ? "not found" : fd.cFileName);
    if (h == INVALID_HANDLE_VALUE) { RET(0xFFFFFFFFu); STDRET(2); return; }
    find_data_out(ARG(1), &fd);
    RET(h2i(h)); STDRET(2);
}
static void k32_FindNextFileA(void) {
    WIN32_FIND_DATAA fd;
    memset(&fd, 0, sizeof(fd));
    HANDLE h = i2h(ARG(0));
    if (!h || !FindNextFileA(h, &fd)) { RET(0); STDRET(2); return; }
    find_data_out(ARG(1), &fd);
    RET(1); STDRET(2);
}
static void k32_FindClose(void) {
    HANDLE h = i2h(ARG(0));
    RET(h ? (FindClose(h) ? 1 : 0) : 0); STDRET(1);
}

/*
 * sscanf, one conversion at a time -- the same shape as crt_format does for
 * printf. Build a single-conversion format with a trailing %n, run the host
 * sscanf on the remaining input, and store the result at the width the
 * conversion implies. The alternative is reimplementing scanf parsing, and a
 * bug in that would present as a bug in the game's own data files.
 */
static void crt_sscanf(void) {
    const char* in = ARGP(0, char);
    const char* fmt = ARGP(1, char);
    uint32_t argva = g_esp + 4 + 2 * 4;
    size_t pos = 0;
    int filled = 0;
    /*
     * sscanf returns EOF, not 0, when the input runs out before the first
     * conversion -- and that is the difference between "this line held no
     * token" and "the file is finished". The game's config reader loops on it,
     * so returning 0 at end of input made the loop take the found-a-token
     * branch with a NULL string and fault on the virtual call through it.
     */
    int eof = 0;

    for (const char* p = fmt; *p; ) {
        if (isspace((unsigned char)*p)) {
            while (isspace((unsigned char)in[pos])) pos++;
            while (isspace((unsigned char)*p)) p++;
            continue;
        }
        if (*p != '%') {                        /* literal: it has to match */
            if (in[pos] != *p) break;
            pos++; p++;
            continue;
        }
        if (p[1] == '%') {
            if (in[pos] != '%') break;
            pos++; p += 2;
            continue;
        }

        const char* start = p++;
        int suppress = 0;
        if (*p == '*') { suppress = 1; p++; }
        while (isdigit((unsigned char)*p)) p++;
        int lng = 0;
        while (*p == 'l' || *p == 'h' || *p == 'L') { if (*p == 'l') lng = 1; p++; }
        char conv = *p++;

        /*
         * %n is not a conversion: it stores how much input has been consumed
         * so far and does not count towards the return value. The game's config
         * reader uses "%4095s %n" and advances its cursor by what %n reports,
         * so leaving that pointer unwritten hands it a garbage offset -- which
         * is how a NULL string reached a virtual call.
         */
        if (conv == 'n') {
            if (!suppress) {
                uint32_t dst = MEM32(argva);
                argva += 4;
                if (dst) MEM32(dst) = (uint32_t)pos;
            }
            continue;
        }

        char spec[32];
        size_t len = (size_t)(p - start);
        if (len >= sizeof(spec) - 3) break;
        memcpy(spec, start, len);
        strcpy(spec + len, "%n");

        int used = -1, n;
        /* Big enough for the widest conversion the game asks for: its config
         * reader uses "%4095s", and a 512-byte scratch buffer was a stack
         * smash that surfaced as a fault inside a system DLL. */
        static union { long i; double d; float f; char s[8192]; } v;
        if (conv == 'f' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G')
            n = lng ? sscanf(in + pos, spec, &v.d, &used)
                    : sscanf(in + pos, spec, &v.f, &used);
        else if (conv == 's' || conv == 'c' || conv == '[')
            n = sscanf(in + pos, spec, v.s, &used);
        else
            n = sscanf(in + pos, spec, &v.i, &used);
        if (n == EOF) eof = 1;
        if (n < 1 && used < 0) break;
        if (used > 0) pos += (size_t)used;
        if (suppress) continue;
        if (n < 1) break;

        uint32_t dst = MEM32(argva);
        argva += 4;
        if (!dst) break;
        char* hd = (char*)(uintptr_t)ADDR(dst);
        switch (conv) {
        case 'f': case 'e': case 'E': case 'g': case 'G':
            if (lng) memcpy(hd, &v.d, 8); else memcpy(hd, &v.f, 4);
            break;
        case 's': case '[': strcpy(hd, v.s); break;
        case 'c': *hd = v.s[0]; break;
        default: {
            uint32_t u = (uint32_t)v.i;
            memcpy(hd, &u, 4);
            break;
        }
        }
        filled++;
    }
    if (g_shim_trace)
        fprintf(stderr, "[crt] sscanf(%.60s | %s) -> %d eof=%d\n", in, fmt, filled, eof);
    RET(filled ? (uint32_t)filled : (eof ? 0xFFFFFFFFu : 0));
    CDECLRET();
}

/* ------------------------------------------------------- KERNEL32 basics */

static void k32_GetModuleHandleA(void) { RET(0x00400000u); STDRET(1); }
/*
 * GetVolumeInformationA. The game's CheckCD compares the volume label against
 * the disc's, and disc 1 is labelled FOCOM_1 (from the ISO primary volume
 * descriptor), so that is what a shim has to report or the game asks for the CD
 * forever. Six of the eight parameters are OUT and each one is only written
 * when its pointer is non-NULL.
 */
static void k32_GetVolumeInformationA(void) {
    if (ARG(1) && ARG(2)) snprintf((char*)(uintptr_t)ADDR(ARG(1)), ARG(2), "FOCOM_1");
    if (ARG(3)) MEM32(ARG(3)) = 0x1A2B3C4Du;          /* any stable serial */
    if (ARG(4)) MEM32(ARG(4)) = 255;
    if (ARG(5)) MEM32(ARG(5)) = 0x00000004u;          /* FILE_READ_ONLY_VOLUME */
    if (ARG(6) && ARG(7)) snprintf((char*)(uintptr_t)ADDR(ARG(6)), ARG(7), "CDFS");
    RET(1); STDRET(8);
}
static void k32_GetTickCount(void)     { RET((uint32_t)GetTickCount()); STDRET(0); }
static void k32_GetLastError(void)     { RET((uint32_t)GetLastError()); STDRET(0); }
static void k32_Sleep(void) {
    /*
     * Sleep(0) has to be a real handoff, not just a released lock.
     *
     * The game's loading loop calls Sleep(0) once per iteration and the mission
     * process runs on another thread. A CRITICAL_SECTION makes no fairness
     * promise, so the main thread released it, Sleep(0) returned without the
     * waiting thread having been scheduled, and the main thread took it
     * straight back. The mission thread starved, the manager reaped it as
     * finished, and the game exited cleanly having run exactly one frame --
     * and it went away when a --calltrace was attached, which is the signature
     * of a scheduling race rather than a logic bug.
     *
     * SwitchToThread yields to a ready thread on this processor, which is
     * precisely what Sleep(0) is asking for, and it returns FALSE if there was
     * nobody to yield to -- so fall back to a 1 ms sleep to guarantee the
     * handoff.
     */
    uint32_t ms = ARG(0);                       /* sampled before releasing */
    mach_yield(ms);
    RET(0); STDRET(1);
}
/*
 * Both of these must return an ABSOLUTE path. The game feeds what they return
 * straight back into _fullpath, so a relative "game" got resolved against
 * g_gamedir a second time and it went looking for game\game\Focom.ini --
 * every path it derived was one directory too deep, and it gave up with
 * exit(0) before opening anything.
 */
static const char* gamedir_abs(char* buf, size_t n) {
    static char abs[MAX_PATH];
    if (!abs[0] && !_fullpath(abs, g_gamedir, sizeof(abs)))
        snprintf(abs, sizeof(abs), "%s", g_gamedir);
    snprintf(buf, n, "%s", abs);
    return buf;
}
static void k32_GetModuleFileNameA(void) {
    char d[MAX_PATH], p[MAX_PATH * 2];
    snprintf(p, sizeof(p), "%s\\Focom.exe", gamedir_abs(d, sizeof(d)));
    snprintf(ARGP(1,char), ARG(2), "%s", p);
    RET((uint32_t)strlen(p)); STDRET(3);
}
static void k32_GetCurrentDirectoryA(void) {
    char d[MAX_PATH];
    gamedir_abs(d, sizeof(d));
    snprintf(ARGP(1,char), ARG(0), "%s", d);
    RET((uint32_t)strlen(d)); STDRET(2);
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

uint32_t ddraw_cocreate(uint32_t clsid_first_dword);   /* ddraw_shims.c */

static void ole_CoCreateInstance(void) {
    uint32_t clsid = ARG(0), ppv = ARG(4);
    uint32_t o = ddraw_cocreate(clsid ? MEM32(clsid) : 0);
    if (ppv) MEM32(ppv) = o;
    RET(o ? 0 : REGDB_E_CLASSNOTREG); STDRET(5);
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
uint32_t ddraw_register_host_proc(import_fn_t fn, const char* name);
static void u32_GetMonitorInfoA(void);

static void k32_GetProcAddress(void) {
    const char* n = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "(ordinal)";
    if (!strcmp(n, "GetMonitorInfoA") || !strcmp(n, "GetMonitorInfoW")) {
        uint32_t mi = ddraw_register_host_proc(u32_GetMonitorInfoA,
                                               "GetMonitorInfoA");
        fprintf(stderr, "[dll] GetProcAddress(\"%s\") -> 0x%08X\n", n, mi);
        RET(mi); STDRET(2);
        return;
    }
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

/* GetMonitorInfoA: the multi-monitor path asks for this by name because it did
 * not exist before Windows 98. MONITORINFO is cbSize, rcMonitor, rcWork,
 * dwFlags -- 40 bytes, and the caller sets cbSize before the call. */
int host_width(void);
int host_height(void);

static void u32_GetMonitorInfoA(void) {
    uint32_t p = ARG(1);
    if (p) {
        MEM32(p + 0x04) = 0;                       /* rcMonitor.left   */
        MEM32(p + 0x08) = 0;                       /* rcMonitor.top    */
        MEM32(p + 0x0C) = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
        MEM32(p + 0x10) = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
        MEM32(p + 0x14) = 0;                       /* rcWork.left      */
        MEM32(p + 0x18) = 0;
        MEM32(p + 0x1C) = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
        MEM32(p + 0x20) = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
        MEM32(p + 0x24) = 1;                       /* MONITORINFOF_PRIMARY */
    }
    RET(1); STDRET(2);
}

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
    { "MSVCRT.dll!_fullpath",   crt_fullpath },
    { "MSVCRT.dll!_splitpath",  crt_splitpath },
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
    { "KERNEL32.dll!GetVolumeInformationA", k32_GetVolumeInformationA },
    { "KERNEL32.dll!CreateFileA",          k32_CreateFileA },
    { "KERNEL32.dll!ReadFile",             k32_ReadFile },
    { "KERNEL32.dll!WriteFile",            k32_WriteFile },
    { "KERNEL32.dll!CloseHandle",          k32_CloseHandle },
    { "KERNEL32.dll!SetFilePointer",       k32_SetFilePointer },
    { "KERNEL32.dll!GetFileSize",          k32_GetFileSize },
    { "KERNEL32.dll!GetFileAttributesA",   k32_GetFileAttributesA },
    { "KERNEL32.dll!FindFirstFileA",       k32_FindFirstFileA },
    { "KERNEL32.dll!FindNextFileA",        k32_FindNextFileA },
    { "KERNEL32.dll!FindClose",            k32_FindClose },
    { "MSVCRT.dll!sscanf",                 crt_sscanf },
    { "ADVAPI32.dll!RegOpenKeyExA",        adv_RegOpenKeyExA },
    { "ADVAPI32.dll!RegCreateKeyExA",      adv_RegCreateKeyExA },
    { "ADVAPI32.dll!RegQueryValueExA",     adv_RegQueryValueExA },
    { "ADVAPI32.dll!RegSetValueExA",       adv_RegSetValueExA },
    { "ADVAPI32.dll!RegCloseKey",          adv_RegCloseKey },
    { "USER32.dll!MessageBoxA",            u32_MessageBoxA },
    { "USER32.dll!GetSystemMetrics",       u32_GetSystemMetrics },
    { "USER32.dll!GetDesktopWindow",       u32_GetDesktopWindow },
    { "USER32.dll!CharNextA",              u32_CharNextA },
    { "USER32.dll!CharPrevA",              u32_CharPrevA },
    { "USER32.dll!ShowCursor",             u32_ShowCursor },
    { "USER32.dll!LoadCursorA",            u32_LoadCursorA },
    { "USER32.dll!SetCursor",              u32_SetCursor },
    { "USER32.dll!GetKeyState",            u32_GetKeyState },
    { "GDI32.dll!GetStockObject",          u32_GetStockObject },
    { "KERNEL32.dll!CreateMutexA",           k32_CreateMutexA },
    { "KERNEL32.dll!ReleaseMutex",           k32_ReleaseMutex },
    { "KERNEL32.dll!CreateEventA",           k32_CreateEventA },
    { "KERNEL32.dll!SetEvent",               k32_SetEvent },
    { "KERNEL32.dll!ResetEvent",             k32_ResetEvent },
    { "KERNEL32.dll!PulseEvent",             k32_PulseEvent },
    { "KERNEL32.dll!WaitForSingleObject",    k32_WaitForSingleObject },
    { "KERNEL32.dll!CopyFileA",              k32_CopyFileA },
    { "KERNEL32.dll!MoveFileA",              k32_MoveFileA },
    { "KERNEL32.dll!RemoveDirectoryA",       k32_RemoveDirectoryA },
    { "KERNEL32.dll!SetFileAttributesA",     k32_SetFileAttributesA },
    { "KERNEL32.dll!FileTimeToLocalFileTime",  k32_FileTimeToLocalFileTime },
    { "KERNEL32.dll!LocalFileTimeToFileTime",  k32_LocalFileTimeToFileTime },
    { "KERNEL32.dll!FileTimeToSystemTime",   k32_FileTimeToSystemTime },
    { "KERNEL32.dll!SystemTimeToFileTime",   k32_SystemTimeToFileTime },
    { "mss32.dll!_AIL_startup@0",                  ail_startup },
    { "mss32.dll!_AIL_shutdown@0",                 ail_shutdown },
    { "mss32.dll!_AIL_last_error@0",               ail_last_error },
    { "mss32.dll!_AIL_set_preference@8",           ail_set_preference },
    { "mss32.dll!_AIL_waveOutOpen@16",             ail_waveOutOpen },
    { "mss32.dll!_AIL_waveOutClose@4",             ail_waveOutClose },
    { "mss32.dll!_AIL_digital_handle_release@4",   ail_digital_handle_release },
    { "mss32.dll!_AIL_digital_handle_reacquire@4", ail_digital_handle_reacquire },
    { "mss32.dll!_AIL_get_DirectSound_info@12",    ail_get_DirectSound_info },
    { "mss32.dll!_AIL_set_DirectSound_HWND@8",     ail_set_DirectSound_HWND },
    { "mss32.dll!_AIL_allocate_sample_handle@4",   ail_allocate_sample_handle },
    { "mss32.dll!_AIL_release_sample_handle@4",    ail_release_sample_handle },
    { "mss32.dll!_AIL_init_sample@4",              ail_init_sample },
    { "mss32.dll!_AIL_set_sample_file@12",         ail_set_sample_file },
    { "mss32.dll!_AIL_set_named_sample_file@20",   ail_set_named_sample_file },
    { "mss32.dll!_AIL_start_sample@4",             ail_start_sample },
    { "mss32.dll!_AIL_end_sample@4",               ail_end_sample },
    { "mss32.dll!_AIL_sample_status@4",            ail_sample_status },
    { "mss32.dll!_AIL_set_sample_volume@8",        ail_set_sample_volume },
    { "mss32.dll!_AIL_set_sample_pan@8",           ail_set_sample_pan },
    { "mss32.dll!_AIL_set_sample_loop_count@8",    ail_set_sample_loop_count },
    { "mss32.dll!_AIL_set_sample_playback_rate@8", ail_set_sample_playback_rate },
    { "WINMM.dll!timeBeginPeriod",                 mm_timeBeginPeriod },
    { "WINMM.dll!timeEndPeriod",                   mm_timeEndPeriod },
    { "WINMM.dll!timeGetDevCaps",                  mm_timeGetDevCaps },
    { "WINMM.dll!timeSetEvent",                    mm_timeSetEvent },
    { "WINMM.dll!timeKillEvent",                   mm_timeKillEvent },
    { "WINMM.dll!PlaySoundA",                      mm_PlaySoundA },
    { "WINMM.dll!mciSendStringA",                  mm_mciSendStringA },
    { "SHELL32.dll!ShellExecuteA",                 sh_ShellExecuteA },
    { "MSVCRT.dll!_purecall",                    crt_purecall },
    { "MSVCRT.dll!_callnewh",                    crt_callnewh },
    { "MSVCRT.dll!?terminate@@YAXXZ",            crt_terminate },
    { "MSVCRT.dll!??1type_info@@UAE@XZ",         crt_typeinfo_dtor },
    { "MSVCRT.dll!??8type_info@@QBEHABV0@@Z",    crt_typeinfo_eq },
    { "MSVCRT.dll!__RTtypeid",                   crt_RTtypeid },
    { "MSVCRT.dll!__RTDynamicCast",              crt_RTDynamicCast },
    { "MSVCRT.dll!_CxxThrowException",           crt_CxxThrowException },
    { "MSVCRT.dll!__CxxFrameHandler",            crt_frame_handler },
    { "MSVCRT.dll!_except_handler3",             crt_except_handler3 },
    { "MSVCRT.dll!bsearch",                      crt_bsearch },
    { "MSVCRT.dll!mktime",                       crt_mktime },
    { "KERNEL32.dll!GetCurrentThreadId",   k32_GetCurrentThreadId },
    { "KERNEL32.dll!GetCurrentThread",     k32_GetCurrentThread },
    { "KERNEL32.dll!lstrlenA",             k32_lstrlenA },
    { "KERNEL32.dll!LocalFree",            k32_LocalFree },
    { "KERNEL32.dll!OutputDebugStringA",   k32_OutputDebugStringA },
    { "KERNEL32.dll!GetLocalTime",         k32_GetLocalTime },
    { "KERNEL32.dll!CreateDirectoryA",     k32_CreateDirectoryA },
    { "KERNEL32.dll!DeleteFileA",          k32_DeleteFileA },
    { "KERNEL32.dll!SetEndOfFile",         k32_SetEndOfFile },
    { "KERNEL32.dll!GlobalMemoryStatus",   k32_GlobalMemoryStatus },
    { "KERNEL32.dll!GetSystemInfo",        k32_GetSystemInfo },
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
