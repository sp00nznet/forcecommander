/*
 * Star Wars: Force Commander - static recompilation host runtime.
 *
 * Owns the recomp machine state, maps the original image, resolves dispatch,
 * and puts a window on screen. Built as a 64-bit host: the target's 32-bit VAs
 * are formed through (uintptr_t) casts, so the original image is mapped 1:1 at
 * its real base and g_mem_base stays 0.
 *
 * The host executable MUST be linked at a high image base (see CMakeLists.txt)
 * so the 0x00400000..0x00C00000 range the target wants is free.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "recomp_types.h"
#include "recomp_trace.h"
#include "imports.h"
#include "image_loader.h"

uint32_t crt_alloc(uint32_t n);   /* crt_shims.c */
void stl_init_data_imports(void);  /* stl_shims.c */

/* ---------------------------------------------------------------- machine */

uint32_t  g_eax = 0, g_ecx = 0, g_edx = 0, g_esp = 0;
uint32_t  g_ebx = 0, g_esi = 0, g_edi = 0, g_ebp = 0;
double    g_st[8] = {0};
int       g_fp_top = 0;
uint16_t  g_fpu_cw = 0x037F;
uint64_t  g_mm[8] = {0};
uint16_t  g_seg_cs = 0, g_seg_ds = 0, g_seg_es = 0;
uint16_t  g_seg_fs = 0, g_seg_gs = 0, g_seg_ss = 0;
uint32_t  g_fs_base = 0, g_gs_base = 0;
ptrdiff_t g_mem_base = 0;

uint32_t  g_cur_func = 0;
uint32_t  g_icall_trace[ICALL_TRACE_SIZE] = {0};
uint32_t  g_icall_trace_idx = 0;
uint32_t  g_icall_count = 0;
/* The generic bring-up diagnostics -- --calltrace, --firsthit, --argtrace,
 * --watch, --poison, --poke -- live in the toolkit now (recomp_trace.c), so
 * the next target gets them without writing them again. What stays here is
 * what only makes sense for this binary. */
/*
 * --scripttrace: one line per executed script line, naming the function.
 *
 * sub_00512170 is GamePPVisBase's "run one line". Its second argument is the
 * compiled line record, [line+4] is the GamePPSysLibraryObj that implements
 * it, and [[line+4]] is that object's vtable -- which analysis/rtti.json maps
 * to a class name, so the trace reads as GamePPGlobalSysWaitForever rather
 * than as an address. --argtrace on the subsystem lookup says which subsystem
 * a line calls; this says which function.
 */
#define VIS_RUN_LINE 0x00512170u
static int g_scripttrace;

static void focom_trace_extra(uint32_t va) {
    if (!g_scripttrace || va != VIS_RUN_LINE) return;
    uint32_t line = MEM32(g_esp + 8);
    uint32_t obj = line >= 0x00200000u ? MEM32(line + 4) : 0;
    uint32_t vt = obj >= 0x00200000u ? MEM32(obj) : 0;
    fprintf(stderr, "[line] t%lu vt=%08X obj=%08X\n",
            GetCurrentThreadId(), vt, obj);
}

int g_list_stubs = 0;
extern int g_no_threads;
extern int g_threadtrace;
extern int g_shim_trace;
extern unsigned g_wait_scale;
extern uint32_t g_stlwatch;
void mach_init(void);
void mach_enter(void);
void mach_leave(void);

/* Target memory layout, from pe_analyze on Focom.exe. */
#define FOCOM_IMAGE_BASE  0x00400000u
#define FOCOM_STACK_BASE  0x00200000u
#define FOCOM_STACK_SIZE  0x00100000u        /* 1 MB */
#define FOCOM_STACK_TOP   (FOCOM_STACK_BASE + FOCOM_STACK_SIZE)
#define FOCOM_HEAP_BASE   0x10000000u
/* 1 GB. The game allocates 513,718 times just loading its object templates,
 * and crt_alloc never reuses a byte -- 128 MB ran out mid-load, after which
 * every buf_new failed, every string came back empty, and the game reported
 * "Duplicated object template found" because all its keys compared equal. The
 * target's address space runs to 0x80000000 before the host's own image, so
 * there is room; MEM_RESERVE without MEM_COMMIT keeps the pages until touched.
 * ponytail: a bump allocator with a bigger bump. Write a real free list if a
 * whole mission needs more than this. */
#define FOCOM_HEAP_SIZE   0x40000000u        /* 1 GB */

/* ---------------------------------------------------------------- dispatch */

recomp_func_t recomp_lookup(uint32_t va) {
    /* The generated table is sorted by VA. */
    uint32_t lo = 0, hi = recomp_dispatch_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t m = recomp_dispatch_table[mid].address;
        if (m == va) return recomp_dispatch_table[mid].func;
        if (m < va) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

/*
 * The hook that makes COM work. A DirectDraw vtable slot holds a synthetic VA
 * in a reserved range that contains no code; this turns it back into a host
 * function, so RECOMP_ICALL through a vtable lands in C. It was an unused stub
 * returning NULL until there was an object model to dispatch.
 */
import_fn_t ddraw_lookup_method(uint32_t va);

recomp_func_t recomp_lookup_manual(uint32_t va) {
    return (recomp_func_t)ddraw_lookup_method(va);
}

/* A hand-written shim (shims_impl.c) beats the generated stub. Resolved once
 * per IAT slot and cached, because this is on the indirect-call path. */
import_fn_t shim_real_import(const char* qualified_name);

static import_fn_t g_resolved[512];
static int g_resolved_done = 0;

static void resolve_imports(void) {
    unsigned real = 0;
    for (unsigned i = 0; i < g_import_count && i < 512; i++) {
        import_fn_t f = shim_real_import(g_imports[i].name);
        g_resolved[i] = f ? f : g_imports[i].fn;
        if (f) real++;
    }
    g_resolved_done = 1;
    printf("  real shims installed:         %u of %u\n", real, g_import_count);
    /* Which imports are still generated stubs. A stub is not neutral -- it
     * returns without filling its out parameters, so the caller reads whatever
     * was on the stack. Listing them up front beats discovering each one as a
     * fault. */
    if (g_list_stubs) {
        for (unsigned i = 0; i < g_import_count && i < 512; i++)
            if (!shim_real_import(g_imports[i].name))
                printf("    stub: %s\n", g_imports[i].name);
    }
}

/* The name of the import the machine is inside. A fault in a host DLL means a
 * shim handed Windows a bad pointer, and without this the only evidence is an
 * address in msvcrt.dll that names neither the shim nor the caller. */

recomp_func_t recomp_lookup_import(uint32_t va) {
    if (!g_resolved_done) resolve_imports();
    for (unsigned i = 0; i < g_import_count; i++)
        if (g_imports[i].iat_va == va) {
            g_cur_import = g_imports[i].name;
            return (i < 512) ? g_resolved[i] : g_imports[i].fn;
        }
    return NULL;
}

/*
 * A function the catalog knows but the current closure did not lift. Naming it
 * and stopping is the point: it says exactly which VA to add next, which a bare
 * "unresolved VA" cannot. See run_lift.py.
 */
void recomp_not_lifted(uint32_t va) {
    fprintf(stderr,
        "\n[not-lifted] sub_%08X  (called from 0x%08X)\n"
        "  Widen the closure:  py -3 run_lift.py --roots 0x%08X\n"
        "  or raise --max, or --all.\n", va, g_cur_func, va);
    exit(2);
}

/* ---------------------------------------------------------------- display */

static HWND    g_hwnd;
static HDC     g_memdc;
static HBITMAP g_dib;
static void*   g_dibbits;
static int     g_w = 640, g_h = 480;

/*
 * The splash screen is a dialog, and its whole appearance lives in the game's
 * own dialog procedure at 0x00401770: WM_INITDIALOG loads BITMAP 102,
 * WM_PAINT StretchBlts it and draws the window title over it twice for a drop
 * shadow. So the host provides the window and forwards messages into the
 * lifted procedure -- the pixels are the game's code, not ours.
 */
#define FOCOM_SPLASH_DLGPROC 0x00401770u

uint32_t h2i(HANDLE h);
uint32_t call_lifted_stdcall4(recomp_func_t f, uint32_t a, uint32_t b,
                              uint32_t c, uint32_t d);

static int g_use_lifted_paint = 0;

static int forward_to_lifted(HWND h, UINT m, WPARAM w, LPARAM l, uint32_t* out) {
    if (!g_use_lifted_paint) return 0;
    recomp_func_t f = recomp_lookup(FOCOM_SPLASH_DLGPROC);
    if (!f) return 0;
    uint32_t save_esp = g_esp;
    *out = call_lifted_stdcall4(f, h2i(h), m, (uint32_t)w, (uint32_t)l);
    if (g_esp != save_esp) {
        fprintf(stderr, "[stack] dlgproc msg 0x%04X left esp at 0x%08X, "
                        "expected 0x%08X\n", m, g_esp, save_esp);
        g_esp = save_esp;
    }
    return 1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    uint32_t r = 0;
    switch (m) {
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_PAINT:
        if (forward_to_lifted(h, m, w, l, &r)) return 0;
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            if (g_memdc) BitBlt(dc, 0, 0, g_w, g_h, g_memdc, 0, 0, SRCCOPY);
            EndPaint(h, &ps);
        }
        return 0;
    default: break;
    }
    return DefWindowProcA(h, m, w, l);
}

/* Create the host window and an 8bpp-into-32bpp backing surface. The game's
 * own renderer has a memory path (RE3D's CDD7MemRenderer), so what it wants
 * from us first is a lockable surface, not Direct3D. */
uint32_t host_create_window(const char* title) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "FocomRecomp";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    /*
     * An ordinary titled window, centred. The splash paint code does:
     *
     *     GetWindowRect(hwnd, &rc);
     *     StretchBlt(dst, 0, 0, rc.right, rc.bottom, src, 0, 0, 640, 480, SRCCOPY);
     *
     * -- it passes right/bottom straight in as the destination width and
     * height, which is only true for a window whose rect starts at (0,0), and
     * the game's own setup guarantees that by positioning the splash
     * full-screen at the origin (GetSystemMetrics + SetWindowPos). Left alone,
     * a window at any other position makes right/bottom screen coordinates and
     * the blit overstretches: the first render asked for 812x675 of a 640x480
     * bitmap and came out zoomed into its top-left corner.
     *
     * The fix is not to move the window to the origin -- that gives a
     * borderless thing sitting on top of everything. It is to make the
     * GetWindowRect shim answer with the rect the game assumes it has, which is
     * the client area at the origin. See imp_GetWindowRect in shims_impl.c.
     */
    RECT r = {0, 0, g_w, g_h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    g_hwnd = CreateWindowExA(0, wc.lpszClassName, title ? title : "Force Commander (recomp)",
                             WS_OVERLAPPEDWINDOW, sx > 0 ? sx : 0, sy > 0 ? sy : 0,
                             ww, wh, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return 0;

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = g_w;
    bi.bmiHeader.biHeight = -g_h;          /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(g_hwnd);
    g_memdc = CreateCompatibleDC(dc);
    g_dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &g_dibbits, NULL, 0);
    SelectObject(g_memdc, g_dib);
    ReleaseDC(g_hwnd, dc);

    ShowWindow(g_hwnd, SW_SHOW);
    return (uint32_t)(uintptr_t)g_hwnd;
}

void host_present(void) {
    if (!g_hwnd) return;
    InvalidateRect(g_hwnd, NULL, FALSE);
    UpdateWindow(g_hwnd);
}

int host_pump(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return 0;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 1;
}

/*
 * Re-make the backing surface when the game calls SetDisplayMode. The client
 * area follows, because the splash path depends on GetWindowRect reporting the
 * client rect and the two must not drift apart.
 */
void host_resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == g_w && h == g_h)) return;
    g_w = w; g_h = h;
    if (g_hwnd) {
        RECT r = {0, 0, w, h};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(g_hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOMOVE | SWP_NOZORDER);
        if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = g_w;
        bi.bmiHeader.biHeight = -g_h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(g_hwnd);
        g_dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &g_dibbits, NULL, 0);
        SelectObject(g_memdc, g_dib);
        ReleaseDC(g_hwnd, dc);
        printf("  host surface -> %dx%d\n", g_w, g_h);
    }
}

void* host_surface(void) { return g_dibbits; }
int   host_width(void)   { return g_w; }
int   host_height(void)  { return g_h; }

/* Grab the window's client area to a BMP, so a render can be checked without
 * a person watching. The reference to compare against is BITMAP 102 pulled
 * straight out of the exe by tools/pe/rsrc.py. */
void host_screenshot(const char* path) {
    if (!g_hwnd || !path) return;
    HDC win = GetDC(g_hwnd);
    HDC mem = CreateCompatibleDC(win);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = g_w;
    bi.bmiHeader.biHeight = -g_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bm = CreateDIBSection(win, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, g_w, g_h, win, 0, 0, SRCCOPY);
    SelectObject(mem, old);

    uint32_t px = (uint32_t)g_w * g_h * 4;
    uint8_t fh[14] = {'B', 'M'};
    uint32_t fsz = 14 + 40 + px, off = 14 + 40;
    memcpy(fh + 2, &fsz, 4);
    memcpy(fh + 10, &off, 4);
    BITMAPINFOHEADER h = bi.bmiHeader;
    h.biHeight = -g_h;
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(fh, 1, 14, f);
        fwrite(&h, 1, 40, f);
        fwrite(bits, 1, px, f);
        fclose(f);
        printf("  screenshot -> %s (%dx%d)\n", path, g_w, g_h);
    }
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(g_hwnd, win);
}

/* ---------------------------------------------------------------- crash */

/* --watchdog SECONDS: a host thread that reports where the machine is and stops
 * the process. A hang leaves no fault to catch, and the entry-trace ring only
 * moves while calls are being made -- a spin loop inside one function makes no
 * calls at all, so the ring stops and looks like a crash that never came. The
 * thread only READS the machine state, so it does not race it meaningfully. */
static DWORD g_watchdog_s = 0;

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    uint32_t last = 0;
    unsigned quiet = 0;
    for (;;) {
        Sleep(1000);
        if (g_enter_idx == last) quiet++; else quiet = 0;
        last = g_enter_idx;
        if (quiet < g_watchdog_s) continue;
        fprintf(stderr, "\n=== watchdog: no lifted call for %u s ===\n", quiet);
        fprintf(stderr, "spinning in: 0x%08X\n", g_cur_func);
        fprintf(stderr, "eax=%08X ecx=%08X edx=%08X ebx=%08X\n",
                g_eax, g_ecx, g_edx, g_ebx);
        fprintf(stderr, "esp=%08X ebp=%08X esi=%08X edi=%08X\n",
                g_esp, g_ebp, g_esi, g_edi);
        fprintf(stderr, "last import entered: %s\n", g_cur_import);
        recomp_dump_trace("watchdog");
        fflush(stderr);
        _exit(3);
    }
}

static LONG CALLBACK veh(EXCEPTION_POINTERS* ep) {
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    /* The host is linked at 0x140000000 with /FIXED:NO, so also print the
     * address addr2line wants: the faulting rip rebased onto the link base.
     * Every generated line carries its target VA in a comment, so this turns
     * "somewhere in 10 million lines of C" into one instruction. */
    uintptr_t hostbase = (uintptr_t)GetModuleHandleA(NULL);
    fprintf(stderr, "\n=== fault 0x%08lX at host rip %p (addr2line 0x%llX) ===\n",
            r->ExceptionCode, r->ExceptionAddress,
            (unsigned long long)(0x140000000ull +
                ((uintptr_t)r->ExceptionAddress - hostbase)));

    /*
     * For an access violation ExceptionInformation[0] is read/write/execute and
     * [1] is the address touched. Without it every one of these looks the same
     * and the only way forward is guessing; with it the address usually says
     * which pointer was wrong, and whether it was in the image, the heap, the
     * stack, or nowhere.
     */
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        r->NumberParameters >= 2) {
        uintptr_t at = r->ExceptionInformation[1];
        static const char* what[] = {"read", "write", "execute"};
        ULONG_PTR op = r->ExceptionInformation[0];
        const char* where = "outside every known region";
        if (at >= FOCOM_IMAGE_BASE && at < FOCOM_IMAGE_BASE + 0x00800000u)
            where = "inside the mapped image";
        else if (at >= FOCOM_STACK_BASE && at < FOCOM_STACK_TOP)
            where = "inside the simulated stack";
        else if (at >= FOCOM_HEAP_BASE && at < FOCOM_HEAP_BASE + FOCOM_HEAP_SIZE)
            where = "inside the simulated heap";
        else if (at < 0x10000)
            where = "a null/low pointer";
        fprintf(stderr, "  %s of 0x%016llX -- %s\n",
                op < 3 ? what[op] : "?", (unsigned long long)at, where);
    }
    /* When the fault is not in our own image the address means nothing on its
     * own -- it is a host DLL, which says the shim layer handed a bad pointer
     * to a real Windows function rather than the lifted code going wrong. */
    HMODULE fm = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)r->ExceptionAddress, &fm) && fm) {
        char mn[MAX_PATH] = {0};
        GetModuleFileNameA(fm, mn, sizeof(mn));
        fprintf(stderr, "  faulting module: %s +0x%llX\n", mn,
                (unsigned long long)((uintptr_t)r->ExceptionAddress - (uintptr_t)fm));
    }
    recomp_trace_flush();
    fprintf(stderr, "current lifted function: 0x%08X\n", g_cur_func);
    fprintf(stderr, "last import entered: %s\n", g_cur_import);
    fprintf(stderr, "eax=%08X ecx=%08X edx=%08X ebx=%08X\n", g_eax, g_ecx, g_edx, g_ebx);
    fprintf(stderr, "esp=%08X ebp=%08X esi=%08X edi=%08X\n", g_esp, g_ebp, g_esi, g_edi);
    fprintf(stderr, "last %d indirect targets:\n", ICALL_TRACE_SIZE);
    for (int i = ICALL_TRACE_SIZE; i > 0; i--) {
        uint32_t idx = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        if (g_icall_trace[idx]) fprintf(stderr, "  0x%08X\n", g_icall_trace[idx]);
    }
    recomp_dump_trace("fault");
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---------------------------------------------------------------- main */

int main(int argc, char** argv) {
    const char* exe = (argc > 1 && argv[1][0] != '-') ? argv[1] : "game/Focom.exe";
    int run = 0, splash = 0, shot = 0;
    const char* shot_path = NULL;
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);   /* the toolkit ones */
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--waitscale") && i + 1 < argc)
            g_wait_scale = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--trace")) g_shim_trace = 1;
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc)
            g_watchdog_s = (DWORD)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--nothreads")) g_no_threads = 1;
        else if (!strcmp(argv[i], "--stubs")) g_list_stubs = 1;
        else if (!strcmp(argv[i], "--threadtrace")) g_threadtrace = 1;
        else if (!strcmp(argv[i], "--scripttrace")) {
            g_scripttrace = 1;
            recomp_trace_extra = focom_trace_extra;
        }
        else if (!strcmp(argv[i], "--stlwatch") && i + 1 < argc)
            g_stlwatch = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--splash")) splash = 1;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            shot = 1; shot_path = argv[++i];
        }
    }

    AddVectoredExceptionHandler(1, veh);
    mach_init();
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));

    printf("Force Commander recomp host\n");
    printf("  lifted functions in dispatch: %u\n", recomp_dispatch_count);
    printf("  import bridges:               %u\n", g_import_count);

    uint32_t span = recomp_load_image(exe, FOCOM_IMAGE_BASE);
    if (!span) { fprintf(stderr, "failed to map %s at 0x%08X\n", exe, FOCOM_IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X + %u bytes\n", exe, FOCOM_IMAGE_BASE, span);

    /* Stack and a simple bump heap, both inside the target's address space. */
    if (!VirtualAlloc((void*)(uintptr_t)FOCOM_STACK_BASE, FOCOM_STACK_SIZE,
                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
        fprintf(stderr, "stack alloc at 0x%08X failed (%lu)\n", FOCOM_STACK_BASE, GetLastError());
        return 1;
    }
    if (!VirtualAlloc((void*)(uintptr_t)FOCOM_HEAP_BASE, FOCOM_HEAP_SIZE,
                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
        fprintf(stderr, "heap alloc at 0x%08X failed (%lu)\n", FOCOM_HEAP_BASE, GetLastError());
        return 1;
    }
    g_esp = FOCOM_STACK_TOP - 0x100;
    printf("  stack 0x%08X heap 0x%08X\n", g_esp, FOCOM_HEAP_BASE);

    /*
     * Populate the IAT. The lifter turns `call dword ptr [0x7C3408]` into
     * RECOMP_ICALL(MEM32(0x7C3408)) -- it calls the slot's *contents*, which is
     * what the instruction does. No Windows loader ran over this image, so
     * those slots still hold their on-disk values (hint/name-table RVAs), and
     * calling one lands on a meaningless address.
     *
     * Writing each slot's own VA into itself makes the contents and the
     * address the same number, so recomp_lookup_import resolves either way.
     */
    for (unsigned i = 0; i < g_import_count; i++)
        MEM32(g_imports[i].iat_va) = g_imports[i].iat_va;
    printf("  IAT slots self-patched:       %u\n", g_import_count);

    /*
     * Data imports are the exception, and getting them wrong is subtle.
     * MSVCRT's `_adjust_fdiv` is an int, not a function: the CRT startup does
     *
     *     mov eax, [_adjust_fdiv_slot]   ; load the pointer
     *     mov eax, [eax]                 ; read the int
     *
     * With the slot pointing at itself that reads back the slot's own address,
     * which is non-zero -- and non-zero means "this CPU has the Pentium FDIV
     * bug", which routes every floating-point divide through MSVC's software
     * workaround. Fury3 hit the same flag from the other direction and its
     * note is blunt about the result: NaN across all FP math.
     *
     * Several need a specific value rather than merely a non-self-referential
     * one, which is why stl_shims.c owns this: std::basic_string::npos must
     * read 0xFFFFFFFF, and _Nullstr must be a real NUL byte, because it is what
     * an empty string's _Ptr points at.
     */
    unsigned ndata = 0;
    for (unsigned i = 0; i < g_import_count; i++)
        if (g_imports[i].conv && !strcmp(g_imports[i].conv, "data")) ndata++;
    printf("  data imports:                 %u\n", ndata);
    stl_init_data_imports();
    void ddraw_init(void);
    ddraw_init();

    /*
     * A simulated TIB. The very first thing the CRT entry does is
     * `mov eax, fs:[0]` -- the head of the SEH chain -- and then writes its own
     * frame back to it. The lifter emits fs: accesses as FS_BASE + offset, so
     * the runtime has to point FS_BASE somewhere writable or the entry point
     * faults on its third instruction.
     *
     * Only fs:[0] is touched on this path. A 4 KB block, with the SEH chain
     * head set to the end-of-chain marker, is enough; a real TIB has stack
     * limits at +4/+8 and the TLS array at +0x2C, which is what to fill in next
     * if something reads them.
     */
    uint32_t tib = crt_alloc(0x1000);
    memset((void*)(uintptr_t)ADDR(tib), 0, 0x1000);
    MEM32(tib + 0x00) = 0xFFFFFFFFu;      /* SEH chain: end of chain */
    MEM32(tib + 0x04) = FOCOM_STACK_TOP;  /* stack base */
    MEM32(tib + 0x08) = FOCOM_STACK_BASE; /* stack limit */
    MEM32(tib + 0x18) = tib;              /* linear address of the TIB itself */
    g_fs_base = tib;
    printf("  simulated TIB at 0x%08X\n", tib);

    if (splash) {
        /*
         * Drive the game's own splash dialog procedure, which is the smallest
         * thing in this binary that produces a picture: the host owns the
         * window, the lifted code at 0x00401770 owns every pixel in it.
         */
        recomp_func_t dp = recomp_lookup(FOCOM_SPLASH_DLGPROC);
        if (!dp) {
            fprintf(stderr, "0x%08X is not in the dispatch table -- lift it first:\n"
                    "  py -3 run_lift.py --catalog analysis/splash_catalog.json "
                    "--roots 0x%08X\n", FOCOM_SPLASH_DLGPROC, FOCOM_SPLASH_DLGPROC);
            return 2;
        }
        /* The window title is what the dialog proc draws over the bitmap. */
        host_create_window("STAR WARS: Force Commander");
        g_use_lifted_paint = 1;

        printf("\n  WM_INITDIALOG -> lifted 0x%08X\n", FOCOM_SPLASH_DLGPROC);
        fflush(stdout);
        uint32_t r = 0;
        forward_to_lifted(g_hwnd, WM_INITDIALOG, 0, 0, &r);
        printf("  returned %u\n", r);

        host_present();
        for (int i = 0; i < 120 && host_pump(); i++) Sleep(16);
        if (shot) host_screenshot(shot_path);
        while (host_pump()) Sleep(16);
        return 0;
    }

    if (!run) {
        printf("\n(dry run: image mapped, machine initialised, nothing executed)\n"
               "pass --run to execute the entry point, or --splash to drive the\n"
               "game's own splash dialog procedure\n");
        return 0;
    }

    host_create_window("Force Commander (recomp)");

    /* Entry point from the PE header, resolved through the dispatch table. */
    extern uint32_t focom_entry_va;   /* generated into recomp_dispatch.c's unit */
    recomp_func_t fn = recomp_lookup(focom_entry_va);
    if (!fn) {
        fprintf(stderr, "entry 0x%08X is not in the dispatch table -- "
                        "it is outside the lifted closure\n", focom_entry_va);
        return 2;
    }
    printf("  entering 0x%08X\n\n", focom_entry_va);
    fflush(stdout);
    /* The main thread owns the machine until it blocks. Without this claim it
     * has no saved slot, and the first window-procedure callback creates one
     * from whatever the registers held at that moment -- every later callback
     * then LOADS that snapshot over the live state. */
    mach_enter();
    fn();
    mach_leave();

    printf("\nentry returned; pumping the window\n");
    while (host_pump()) Sleep(16);
    return 0;
}
