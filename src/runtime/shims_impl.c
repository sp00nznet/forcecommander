/*
 * Force Commander - hand-written shims.
 *
 * Two kinds live here:
 *   1. `sub_XXXXXXXX` bodies for functions in HOST_SHIM (run_lift.py) that are
 *      better implemented than lifted.
 *   2. Real bodies for the USER32/GDI32 imports the splash dialog uses, so the
 *      game's own paint code draws through the host's real Win32.
 *
 * Handles are the interesting part. The lifted code is 32-bit and holds HWND,
 * HDC and HBITMAP as uint32; on a 64-bit host those are 64-bit pointers. So
 * every handle crossing the boundary goes through a table and the lifted side
 * only ever sees a small integer. Casting a HANDLE down to 32 bits and back
 * happens to work on current Windows -- user and GDI handles are allocated low
 * -- and that is exactly the kind of thing that works until it does not.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "recomp_types.h"
#include "imports.h"

uint32_t crt_alloc(uint32_t n);   /* crt_shims.c */
extern int g_shim_trace;   /* --trace */

/* The machine lock, defined with the thread support further down. Declared
 * here because the blocking USER32 shims come first in this file. */
void mach_enter(void);
void mach_leave(void);
void mach_yield(uint32_t sleep_ms);
int  mach_depth(void);
#define BLOCKING(expr) do { mach_leave(); (expr); mach_enter(); } while (0)

/* ---------------------------------------------------------- handle table */

#define HT_MAX 256
static HANDLE  ht[HT_MAX];
static unsigned ht_n = 1;                  /* 0 stays NULL */

uint32_t h2i(HANDLE h) {
    if (!h) return 0;
    for (unsigned i = 1; i < ht_n; i++) if (ht[i] == h) return i;
    if (ht_n >= HT_MAX) { fprintf(stderr, "[shim] handle table full\n"); return 0; }
    ht[ht_n] = h;
    return ht_n++;
}

HANDLE i2h(uint32_t i) {
    return (i && i < ht_n) ? ht[i] : NULL;
}

/* ------------------------------------------------------------ __chkstk */

/*
 * MSVC _alloca_probe. The original walks the requested frame touching a dword
 * per page so the guard page faults in order, then relocates the return address
 * to the new stack top and returns through it. The host stack is ordinary
 * committed memory, so the probing is unnecessary; what the caller depends on is
 * the net effect, which is "esp drops by EAX and the return still works".
 *
 * On entry g_esp points at the dummy return address RECOMP_CALL pushed.
 */
void sub_0056EB30(void) {
    RECOMP_ENTER(0x0056EB30u);
    g_esp += 4;                 /* consume the dummy return address */
    g_esp -= g_eax;             /* allocate the frame */
}

/* ------------------------------------------- calling into lifted code */

/* stdcall with 4 args: the callee's `ret 0x10` pops the dummy + 4 slots. */
uint32_t call_lifted_stdcall4(recomp_func_t f, uint32_t a, uint32_t b,
                              uint32_t c, uint32_t d) {
    PUSH32(g_esp, d);
    PUSH32(g_esp, c);
    PUSH32(g_esp, b);
    PUSH32(g_esp, a);
    PUSH32(g_esp, RECOMP_RETADDR);
    f();
    return g_eax;
}

/* ------------------------------------------------------- USER32 / GDI32 */

/*
 * LoadBitmapA(hInstance, id). The resources live in our mapped copy of
 * Focom.exe, not in a real loaded module, so FindResource cannot be used --
 * the image was mapped by image_loader.c, which maps sections, not a module.
 * Walk the PE resource directory in the mapped image instead and hand the
 * BITMAPINFOHEADER to CreateDIBitmap.
 *
 * A RT_BITMAP resource has no BITMAPFILEHEADER: the loader knows where the
 * pixels start so the file does not say. And its biClrUsed is 0, meaning "as
 * many entries as the bit depth allows" -- 256 here -- so the palette size has
 * to be computed, not read.
 */
#define IMAGE_BASE 0x00400000u
#define RT_BITMAP_ID 2

static const uint8_t* find_resource(uint32_t type, uint32_t name, uint32_t* size) {
    const uint8_t* img = (const uint8_t*)(uintptr_t)IMAGE_BASE;
    uint32_t pe = *(const uint32_t*)(img + 0x3C);
    const uint8_t* nt = img + pe;
    uint16_t optsz = *(const uint16_t*)(nt + 0x14);
    const uint8_t* opt = nt + 0x18;
    uint16_t magic = *(const uint16_t*)opt;
    const uint8_t* dd = opt + (magic == 0x10B ? 96 : 112);
    uint32_t rsrc_rva = *(const uint32_t*)(dd + 2 * 8);
    if (!rsrc_rva) return NULL;
    const uint8_t* root = img + rsrc_rva;
    (void)optsz;

    /* Three levels: type -> name -> language. Each is a directory of entries. */
    const uint8_t* lvl = root;
    uint32_t want[3] = {type, name, 0};
    for (int depth = 0; depth < 3; depth++) {
        uint16_t nnamed = *(const uint16_t*)(lvl + 12);
        uint16_t nid    = *(const uint16_t*)(lvl + 14);
        const uint8_t* ent = lvl + 16;
        const uint8_t* hit = NULL;
        for (int i = 0; i < nnamed + nid; i++, ent += 8) {
            uint32_t id  = *(const uint32_t*)(ent + 0);
            uint32_t off = *(const uint32_t*)(ent + 4);
            if (id & 0x80000000u) continue;                 /* named, skip */
            if (depth < 2 && id != want[depth]) continue;   /* wrong type/name */
            hit = root + (off & 0x7FFFFFFFu);
            if (depth == 2 || !(off & 0x80000000u)) {
                if (depth == 2 || depth == 1) {
                    /* leaf: a data entry, not a directory */
                    if (!(off & 0x80000000u)) {
                        const uint8_t* de = root + off;
                        *size = *(const uint32_t*)(de + 4);
                        return img + *(const uint32_t*)(de + 0);
                    }
                }
            }
            break;
        }
        if (!hit) return NULL;
        lvl = hit;
    }
    return NULL;
}

static void imp_LoadBitmapA(void) {
    uint32_t id = ARG(1);
    uint32_t sz = 0;
    const uint8_t* res = find_resource(RT_BITMAP_ID, id, &sz);
    if (!res) {
        fprintf(stderr, "[shim] LoadBitmapA(%u): resource not found\n", id);
        RET(0); STDRET(2); return;
    }
    const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)res;
    uint32_t ncolors = bih->biClrUsed ? bih->biClrUsed
                     : (bih->biBitCount <= 8 ? (1u << bih->biBitCount) : 0);
    const uint8_t* bits = res + bih->biSize + ncolors * 4;

    HDC screen = GetDC(NULL);
    HBITMAP bmp = CreateDIBitmap(screen, bih, CBM_INIT, bits,
                                 (const BITMAPINFO*)bih, DIB_RGB_COLORS);
    ReleaseDC(NULL, screen);
    fprintf(stderr, "[shim] LoadBitmapA(%u): %ldx%ld %ubpp, %u colors, %u bytes -> %p\n",
            id, bih->biWidth, bih->biHeight, bih->biBitCount, ncolors, sz, (void*)bmp);
    RET(h2i(bmp));
    STDRET(2);
}

/* Straight pass-throughs. Handles in, handles out, through the table. */

static void imp_GetUpdateRect(void) {
    RECT* r = ARGP(1, RECT);
    BOOL ok = GetUpdateRect((HWND)i2h(ARG(0)), r, (BOOL)ARG(2));
    RET(ok); STDRET(3);
}

static void imp_BeginPaint(void) {
    /* PAINTSTRUCT differs in size between 32- and 64-bit (HDC is a pointer), so
     * the game's 32-bit struct cannot be handed to the real BeginPaint. Paint
     * into a host-side PAINTSTRUCT and write back only the fields the game
     * reads: hdc at +0, fErase at +4, rcPaint at +8. */
    static PAINTSTRUCT ps;
    HWND h = (HWND)i2h(ARG(0));
    HDC dc = BeginPaint(h, &ps);
    uint32_t out = ARG(1);
    MEM32(out + 0) = h2i(dc);
    MEM32(out + 4) = (uint32_t)ps.fErase;
    MEM32(out + 8) = (uint32_t)ps.rcPaint.left;
    MEM32(out + 12) = (uint32_t)ps.rcPaint.top;
    MEM32(out + 16) = (uint32_t)ps.rcPaint.right;
    MEM32(out + 20) = (uint32_t)ps.rcPaint.bottom;
    RET(h2i(dc)); STDRET(2);
}

static void imp_EndPaint(void) {
    static PAINTSTRUCT ps;
    EndPaint((HWND)i2h(ARG(0)), &ps);
    RET(1); STDRET(2);
}

/*
 * GetWindowRect, answered as the client area at the origin: (0, 0, cw, ch).
 *
 * Deliberately not the real window rect. The splash paint code passes
 * rc.right/rc.bottom straight into StretchBlt as the destination width and
 * height, which is only correct for a window positioned at (0,0) -- and the
 * game guarantees that for itself by putting the splash full-screen at the
 * origin. Handing back true screen coordinates makes it overstretch by however
 * far the window is from the corner, so the host would have to be a borderless
 * overlay in the top-left to look right.
 *
 * Reporting the client rect instead gives the game exactly the rect its
 * assumption describes, and the blit fills the client area 1:1 with the window
 * anywhere on screen, with a title bar, movable.
 *
 * The ceiling: code that uses GetWindowRect to *position* something, or to work
 * out where the window is relative to the desktop, will get the wrong answer.
 * Nothing on the splash path does. Revisit when something does -- at that point
 * this needs to become "real rect, and fix up the blit", not a bigger lie.
 * ponytail: client-rect-as-window-rect, narrow and load-bearing.
 */
static void imp_GetWindowRect(void) {
    RECT* r = ARGP(1, RECT);
    BOOL ok = GetClientRect((HWND)i2h(ARG(0)), r);   /* already origin-based */
    RET(ok); STDRET(2);
}

static void imp_GetWindowTextA(void) {
    RET(GetWindowTextA((HWND)i2h(ARG(0)), ARGP(1, char), (int)ARG(2)));
    STDRET(3);
}

static void imp_CreateCompatibleDC(void) {
    RET(h2i(CreateCompatibleDC((HDC)i2h(ARG(0))))); STDRET(1);
}

static void imp_SelectObject(void) {
    RET(h2i(SelectObject((HDC)i2h(ARG(0)), i2h(ARG(1))))); STDRET(2);
}

static void imp_DeleteDC(void) { RET(DeleteDC((HDC)i2h(ARG(0)))); STDRET(1); }
static void imp_DeleteObject(void) { RET(DeleteObject(i2h(ARG(0)))); STDRET(1); }

static void imp_StretchBlt(void) {
    BOOL ok = StretchBlt((HDC)i2h(ARG(0)), (int)ARG(1), (int)ARG(2),
                         (int)ARG(3), (int)ARG(4),
                         (HDC)i2h(ARG(5)), (int)ARG(6), (int)ARG(7),
                         (int)ARG(8), (int)ARG(9), (DWORD)ARG(10));
    fprintf(stderr, "[shim] StretchBlt dst=%d,%d %dx%d src=%d,%d %dx%d rop=%08X -> %d\n",
            (int)ARG(1), (int)ARG(2), (int)ARG(3), (int)ARG(4),
            (int)ARG(6), (int)ARG(7), (int)ARG(8), (int)ARG(9),
            (unsigned)ARG(10), ok);
    RET(ok); STDRET(11);
}

static void imp_CreateFontA(void) {
    HFONT f = CreateFontA((int)ARG(0), (int)ARG(1), (int)ARG(2), (int)ARG(3),
                          (int)ARG(4), (DWORD)ARG(5), (DWORD)ARG(6), (DWORD)ARG(7),
                          (DWORD)ARG(8), (DWORD)ARG(9), (DWORD)ARG(10),
                          (DWORD)ARG(11), (DWORD)ARG(12), ARGP(13, char));
    RET(h2i(f)); STDRET(14);
}

static void imp_SetBkMode(void) {
    RET(SetBkMode((HDC)i2h(ARG(0)), (int)ARG(1))); STDRET(2);
}

static void imp_SetTextColor(void) {
    RET(SetTextColor((HDC)i2h(ARG(0)), (COLORREF)ARG(1))); STDRET(2);
}

static void imp_GetTextExtentPoint32A(void) {
    SIZE* s = ARGP(3, SIZE);
    RET(GetTextExtentPoint32A((HDC)i2h(ARG(0)), ARGP(1, char), (int)ARG(2), s));
    STDRET(4);
}

static void imp_TextOutA(void) {
    BOOL ok = TextOutA((HDC)i2h(ARG(0)), (int)ARG(1), (int)ARG(2),
                       ARGP(3, char), (int)ARG(4));
    RET(ok); STDRET(5);
}

/* The bridge consults this before the generated table, so these win. */
/* ------------------------------------------------- USER32 windows and messages

 * The game creates its own window class and window, pumps its own message loop
 * and shows its own loading dialog. All of that has to be real, because the
 * window procedure it registers is LIFTED CODE: a target VA that the host has
 * to call back into whenever Windows delivers a message.
 *
 * The bridge is win_trampoline(): one real WNDPROC registered for every class
 * the game creates, which looks the target procedure up per window and calls it
 * through the simulated stack. GWLP_USERDATA is not usable for that -- the game
 * uses it itself -- so the mapping lives in a small table.
 *
 * 32-bit struct layouts that matter here (the host's differ, so none of these
 * can be memcpy'd):
 *   WNDCLASSA    style, lpfnWndProc, cbClsExtra, cbWndExtra, hInstance, hIcon,
 *                hCursor, hbrBackground, lpszMenuName, lpszClassName   = 40
 *   WNDCLASSEXA  cbSize first, hIconSm last                            = 48
 *   MSG          hwnd, message, wParam, lParam, time, pt.x, pt.y       = 28
 */
#define MSG32_SIZE 28

/*
 * Per window: the lifted procedure, and the style the GAME asked for.
 *
 * The host window is deliberately not created with that style -- a WS_POPUP
 * full-screen window sits at the origin on top of everything, which is not
 * what anyone wants out of a bring-up. But the game reads its own style back
 * with GetWindowLongA(GWL_STYLE) and branches on it, and handing it the
 * windowed style we substituted sent it down a path that dereferenced 0.
 *
 * So it is told what it asked for. This is the same bargain imp_GetWindowRect
 * already makes: the window the game believes it has is full-screen at the
 * origin, and the one on screen is an ordinary titled window.
 */
static struct { HWND h; uint32_t proc, style, ex_style; } g_winproc[64];
static unsigned g_winproc_n = 0;

static uint32_t win_target_proc(HWND h) {
    for (unsigned i = 0; i < g_winproc_n; i++)
        if (g_winproc[i].h == h) return g_winproc[i].proc;
    return 0;
}
static void win_bind(HWND h, uint32_t proc) {
    for (unsigned i = 0; i < g_winproc_n; i++)
        if (g_winproc[i].h == h) { g_winproc[i].proc = proc; return; }
    if (g_winproc_n < 64) { g_winproc[g_winproc_n].h = h;
                            g_winproc[g_winproc_n++].proc = proc; }
}

static void win_bind_style(HWND h, uint32_t style, uint32_t ex_style) {
    for (unsigned i = 0; i < g_winproc_n; i++)
        if (g_winproc[i].h == h) {
            g_winproc[i].style = style;
            g_winproc[i].ex_style = ex_style;
            return;
        }
}
/* The style the game set, or 0 if this window is not one of its own. */
static uint32_t win_style(HWND h, int ex) {
    for (unsigned i = 0; i < g_winproc_n; i++)
        if (g_winproc[i].h == h)
            return ex ? g_winproc[i].ex_style : g_winproc[i].style;
    return 0;
}

/* The class name -> target procedure map, filled by RegisterClass(Ex)A and read
 * by CreateWindowExA, because a window's procedure comes from its class. */
static struct { char name[64]; uint32_t proc; } g_class[32];
static unsigned g_class_n = 0;

static void class_bind(const char* name, uint32_t proc) {
    for (unsigned i = 0; i < g_class_n; i++)
        if (!strcmp(g_class[i].name, name)) { g_class[i].proc = proc; return; }
    if (g_class_n < 32) {
        snprintf(g_class[g_class_n].name, sizeof(g_class[0].name), "%s", name);
        g_class[g_class_n++].proc = proc;
    }
}
static uint32_t class_proc(const char* name) {
    for (unsigned i = 0; i < g_class_n; i++)
        if (!strcmp(g_class[i].name, name)) return g_class[i].proc;
    return 0;
}

/*
 * Messages whose lParam is a POINTER cannot be passed through.
 *
 * WM_NCCREATE and WM_CREATE carry a CREATESTRUCTA*, and it is a HOST pointer --
 * truncating it to 32 bits and handing it to the game gave its window procedure
 * 0x953FD330 to dereference. Nothing from the host address space may be visible
 * to the target; this is the same rule that WIN32_FIND_DATAA broke.
 *
 * CREATESTRUCTA in the 32-bit ABI is twelve dwords:
 *   +00 lpCreateParams  +04 hInstance  +08 hMenu    +0C hwndParent
 *   +10 cy              +14 cx         +18 y        +1C x
 *   +20 style           +24 lpszName   +28 lpszClass +2C dwExStyle
 *
 * The two string fields are the interesting part: the game passed those
 * pointers INTO CreateWindowExA as target addresses, so the originals are what
 * belong here, not the host copies Windows echoes back. They are kept aside at
 * the call.
 */
static struct {
    uint32_t name, cls, params, style, ex_style;
} g_pending_create;

/*
 * The translation has to work in BOTH directions. A window procedure normally
 * ends by handing the message to DefWindowProcA unchanged -- including the
 * lParam it was given, which is now the target CREATESTRUCT. Passing that on
 * had USER32 dereference 0xFFFFFFFF903FD710, our own buffer address
 * sign-extended. So the pairing is remembered for the duration of the message
 * and undone on the way back out.
 */
static LPARAM   g_msg_host_lp;
static uint32_t g_msg_target_lp;

static uint32_t create_struct_to_target(const CREATESTRUCTA* cs) {
    static uint32_t buf;                 /* one window at a time is created */
    if (!buf) buf = crt_alloc(0x30);
    if (!buf) return 0;
    MEM32(buf + 0x00) = g_pending_create.params;
    MEM32(buf + 0x04) = 0x00400000u;                  /* hInstance: the image */
    MEM32(buf + 0x08) = h2i(cs->hMenu);
    MEM32(buf + 0x0C) = h2i(cs->hwndParent);
    MEM32(buf + 0x10) = (uint32_t)cs->cy;
    MEM32(buf + 0x14) = (uint32_t)cs->cx;
    MEM32(buf + 0x18) = (uint32_t)cs->y;
    MEM32(buf + 0x1C) = (uint32_t)cs->x;
    MEM32(buf + 0x20) = g_pending_create.style;
    MEM32(buf + 0x24) = g_pending_create.name;
    MEM32(buf + 0x28) = g_pending_create.cls;
    MEM32(buf + 0x2C) = g_pending_create.ex_style;
    return buf;
}

static LRESULT CALLBACK win_trampoline(HWND h, UINT m, WPARAM w, LPARAM l) {
    uint32_t va = win_target_proc(h);
    if (!va && m == WM_NCCREATE) {
        /* The binding cannot exist before CreateWindowEx returns, so the first
         * few messages arrive unbound; fall back to the class of the window
         * being created. */
        char cn[64] = {0};
        GetClassNameA(h, cn, sizeof(cn));
        va = class_proc(cn);
        if (va) win_bind(h, va);
    }
    if (m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) {
        static unsigned n;
        if (n++ < 12)
            fprintf(stderr, "[wp] msg 0x%04X l=%08X hwnd=%p va=0x%08X\n",
                    m, (uint32_t)l, (void*)h, va);
    }
    recomp_func_t f = va ? recomp_lookup(va) : NULL;
    if (!f) return DefWindowProcA(h, m, w, l);

    /* Windows can call this back while the machine is released (a message
     * dispatched from inside GetMessageA, or host_pump after the entry point
     * returned), so the trampoline claims it. Nested claims are free. */
    mach_enter();
    uint32_t lp = (uint32_t)l;
    if ((m == WM_NCCREATE || m == WM_CREATE) && l)
        lp = create_struct_to_target((const CREATESTRUCTA*)l);
    LPARAM   save_host_lp = g_msg_host_lp;
    uint32_t save_target_lp = g_msg_target_lp;
    g_msg_host_lp = l;
    g_msg_target_lp = lp;
    uint32_t save_esp = g_esp, save_fn = g_cur_func;
    uint32_t r = call_lifted_stdcall4(f, h2i(h), m, (uint32_t)w, lp);
    g_cur_func = save_fn;
    if (g_esp != save_esp) {
        fprintf(stderr, "[user32] wndproc 0x%08X msg 0x%04X left esp at "
                        "0x%08X, expected 0x%08X\n", va, m, g_esp, save_esp);
        g_esp = save_esp;
    }
    g_msg_host_lp = save_host_lp;
    g_msg_target_lp = save_target_lp;
    mach_leave();
    return (LRESULT)(int32_t)r;
}

/*
 * The game's main window, and why the host has to know about it.
 *
 * There are two windows. The host makes one at startup, because something has
 * to exist before the game does anything, and the game then makes its own
 * through CreateWindowExA -- bound here to its real window procedure through
 * win_trampoline. Windows delivers WM_MOUSEMOVE and WM_LBUTTONDOWN to whatever
 * window the pointer is over, and that is the host's, whose procedure does not
 * know the game exists. Pixels went to one window and input to the other.
 *
 * Presenting into the game's window instead was tried and is much worse: the
 * window belongs to the thread that created it, cross-thread GDI to it dropped
 * the frame rate from 3,000 presents a run to 39, and the ShowWindow needed to
 * make it visible SENDS a message and blocks on that thread's pump.
 *
 * So the host window keeps the pixels and forwards the input. This is the
 * handle pair it needs.
 */
static HWND     g_win_main;
static uint32_t g_win_main_proc;

static void win_main_set(HWND h, uint32_t proc) {
    g_win_main = h;
    g_win_main_proc = proc;
}

void* win_main_hwnd(void) { return (void*)g_win_main; }

uint32_t win_main(uint32_t* target_hwnd) {
    if (target_hwnd) *target_hwnd = g_win_main ? h2i(g_win_main) : 0;
    return g_win_main_proc;
}

static void u32_RegisterClassA(void) {
    uint32_t c = ARG(0);
    if (!c) { RET(0); STDRET(1); return; }
    uint32_t nameva = MEM32(c + 36);
    const char* name = nameva ? (const char*)(uintptr_t)ADDR(nameva) : "";
    class_bind(name, MEM32(c + 4));
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = MEM32(c + 0);
    wc.lpfnWndProc = win_trampoline;
    wc.cbClsExtra = (int)MEM32(c + 8);
    wc.cbWndExtra = (int)MEM32(c + 12);
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = name;
    if (g_shim_trace)
        fprintf(stderr, "[user32] RegisterClassA(\"%s\") proc=0x%08X\n",
            name, MEM32(c + 4));
    RET((uint32_t)RegisterClassA(&wc)); STDRET(1);
}
static void u32_RegisterClassExA(void) {
    uint32_t c = ARG(0);
    if (!c) { RET(0); STDRET(1); return; }
    uint32_t nameva = MEM32(c + 40);
    const char* name = nameva ? (const char*)(uintptr_t)ADDR(nameva) : "";
    class_bind(name, MEM32(c + 8));
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = MEM32(c + 4);
    wc.lpfnWndProc = win_trampoline;
    wc.cbClsExtra = (int)MEM32(c + 12);
    wc.cbWndExtra = (int)MEM32(c + 16);
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = name;
    fprintf(stderr, "[user32] RegisterClassExA(\"%s\") proc=0x%08X\n",
            name, MEM32(c + 8));
    RET((uint32_t)RegisterClassExA(&wc)); STDRET(1);
}
static void u32_CreateWindowExA(void) {
    const char* cls = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "";
    const char* title = ARG(2) ? (const char*)(uintptr_t)ADDR(ARG(2)) : "";
    /* WS_POPUP full-screen windows land at the origin on top of everything, so
     * the style is forced to an ordinary titled window, the same decision
     * host_create_window() already made for the splash. */
    DWORD style = (ARG(3) & ~(DWORD)WS_POPUP) | WS_OVERLAPPEDWINDOW;
    /* Kept for the CREATESTRUCT the game's own procedure is about to be
     * handed: these are target addresses, and Windows would echo back host
     * copies of the strings. */
    g_pending_create.ex_style = ARG(0);
    g_pending_create.cls = ARG(1);
    g_pending_create.name = ARG(2);
    g_pending_create.style = ARG(3);
    g_pending_create.params = ARG(11);
    HWND h = CreateWindowExA(ARG(0), cls, title, style,
                             (int)ARG(4), (int)ARG(5), (int)ARG(6), (int)ARG(7),
                             ARG(8) ? i2h(ARG(8)) : NULL, NULL,
                             GetModuleHandleA(NULL), NULL);
    fprintf(stderr, "[user32] CreateWindowExA(\"%s\", \"%s\") -> %p\n",
            cls, title, (void*)h);
    if (!h) { RET(0); STDRET(12); return; }
    uint32_t proc = class_proc(cls);
    if (proc) win_bind(h, proc);
    else win_bind(h, 0);                 /* still record it, for the style */
    win_bind_style(h, ARG(3), ARG(0));
    /* Remember the game's main window so the host's own window can forward
     * input to its procedure. See win_main() and host_input(). */
    if (proc && !ARG(8)) win_main_set(h, proc);
    RET(h2i(h)); STDRET(12);
}
static void u32_DefWindowProcA(void) {
    /* Hand back the host pointer if this is the lParam we translated on the
     * way in; anything else is the game's own value and passes through. */
    LPARAM lp = (ARG(3) && ARG(3) == g_msg_target_lp) ? g_msg_host_lp
                                                      : (LPARAM)(int32_t)ARG(3);
    RET((uint32_t)(int32_t)DefWindowProcA(i2h(ARG(0)), ARG(1),
                                          (WPARAM)ARG(2), lp));
    STDRET(4);
}
static void u32_DestroyWindow(void) { RET(DestroyWindow(i2h(ARG(0))) ? 1 : 0); STDRET(1); }
static void u32_ShowWindow(void)    { RET(ShowWindow(i2h(ARG(0)), (int)ARG(1)) ? 1 : 0); STDRET(2); }
static void u32_UpdateWindow(void)  { RET(UpdateWindow(i2h(ARG(0))) ? 1 : 0); STDRET(1); }
static void u32_IsWindow(void)      { RET(IsWindow(i2h(ARG(0))) ? 1 : 0); STDRET(1); }
static void u32_IsIconic(void)      { RET(IsIconic(i2h(ARG(0))) ? 1 : 0); STDRET(1); }
static void u32_IsWindowVisible(void){ RET(IsWindowVisible(i2h(ARG(0))) ? 1 : 0); STDRET(1); }
static void u32_GetParent(void)     { RET(h2i(GetParent(i2h(ARG(0))))); STDRET(1); }
static void u32_SetParent(void)     { RET(h2i(SetParent(i2h(ARG(0)), i2h(ARG(1))))); STDRET(2); }
static void u32_SetWindowTextA(void) {
    const char* t = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "";
    RET(SetWindowTextA(i2h(ARG(0)), t) ? 1 : 0); STDRET(2);
}
static void u32_SetWindowPos(void) {
    RET(SetWindowPos(i2h(ARG(0)), ARG(1) ? i2h(ARG(1)) : NULL,
                     (int)ARG(2), (int)ARG(3), (int)ARG(4), (int)ARG(5),
                     ARG(6)) ? 1 : 0);
    STDRET(7);
}
/*
 * GWLP_WNDPROC and GWLP_HINSTANCE are the two indices that must not pass
 * through: the real answers are win_trampoline and the host module, neither of
 * which the game may see. It gets its OWN procedure VA and its own image base,
 * which is what it put there.
 *
 * SetWindowLongA(GWLP_WNDPROC) is subclassing: rebind the window to the new
 * target procedure and leave the host trampoline installed, then report the
 * previous target VA so a subclass chain still works.
 */
static void u32_GetWindowLongA(void) {
    int idx = (int)ARG(1);
    HWND h = i2h(ARG(0));
    if (idx == GWLP_WNDPROC)   { RET(win_target_proc(h)); STDRET(2); return; }
    if (idx == GWLP_HINSTANCE) { RET(0x00400000u);        STDRET(2); return; }
    if (idx == GWL_STYLE || idx == GWL_EXSTYLE) {
        uint32_t s = win_style(h, idx == GWL_EXSTYLE);
        if (s) { RET(s); STDRET(2); return; }
    }
    uint32_t v = (uint32_t)GetWindowLongA(h, idx);
    RET(v); STDRET(2);
}
static void u32_SetWindowLongA(void) {
    int idx = (int)ARG(1);
    HWND h = i2h(ARG(0));
    if (idx == GWLP_WNDPROC) {
        uint32_t prev = win_target_proc(h);
        win_bind(h, ARG(2));
        RET(prev); STDRET(3); return;
    }
    if (idx == GWLP_HINSTANCE) { RET(0x00400000u); STDRET(3); return; }
    if (idx == GWL_STYLE || idx == GWL_EXSTYLE) {
        /* Record it and report the old one; the host window keeps the style
         * that keeps it on screen as a window. */
        uint32_t prev = win_style(h, idx == GWL_EXSTYLE);
        if (idx == GWL_STYLE) win_bind_style(h, ARG(2), win_style(h, 1));
        else                  win_bind_style(h, win_style(h, 0), ARG(2));
        RET(prev); STDRET(3); return;
    }
    RET((uint32_t)SetWindowLongA(h, idx, (LONG)ARG(2))); STDRET(3);
}
static void u32_GetClientRect(void) {
    RECT r = {0, 0, 0, 0};
    GetClientRect(i2h(ARG(0)), &r);
    if (ARG(1)) { uint32_t o = ARG(1);
        MEM32(o) = (uint32_t)r.left; MEM32(o + 4) = (uint32_t)r.top;
        MEM32(o + 8) = (uint32_t)r.right; MEM32(o + 12) = (uint32_t)r.bottom; }
    RET(1); STDRET(2);
}
static void u32_ClientToScreen(void) {
    POINT p = {0, 0};
    if (ARG(1)) { p.x = (LONG)MEM32(ARG(1)); p.y = (LONG)MEM32(ARG(1) + 4); }
    ClientToScreen(i2h(ARG(0)), &p);
    if (ARG(1)) { MEM32(ARG(1)) = (uint32_t)p.x; MEM32(ARG(1) + 4) = (uint32_t)p.y; }
    RET(1); STDRET(2);
}
static void u32_SetCursorPos(void) { RET(SetCursorPos((int)ARG(0), (int)ARG(1)) ? 1 : 0); STDRET(2); }
static void u32_GetPropA(void) {
    const char* n = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "";
    RET(h2i(GetPropA(i2h(ARG(0)), n))); STDRET(2);
}
static void u32_SetPropA(void) {
    const char* n = ARG(1) ? (const char*)(uintptr_t)ADDR(ARG(1)) : "";
    RET(SetPropA(i2h(ARG(0)), n, i2h(ARG(2))) ? 1 : 0); STDRET(3);
}
/* WINDOWPLACEMENT has no pointers; length, flags, showCmd, 2 POINTs, 1 RECT. */
static void u32_GetWindowPlacement(void) {
    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof(wp));
    wp.length = sizeof(wp);
    BOOL ok = GetWindowPlacement(i2h(ARG(0)), &wp);
    if (ARG(1)) memcpy((void*)(uintptr_t)ADDR(ARG(1)), &wp, sizeof(wp));
    RET(ok ? 1 : 0); STDRET(2);
}
static void u32_SetWindowPlacement(void) {
    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof(wp));
    if (ARG(1)) memcpy(&wp, (void*)(uintptr_t)ADDR(ARG(1)), sizeof(wp));
    wp.length = sizeof(wp);
    RET(SetWindowPlacement(i2h(ARG(0)), &wp) ? 1 : 0); STDRET(2);
}

/* --- the message loop. MSG is 28 bytes in the target, not the host's 48. --- */
static void msg_out(uint32_t va, const MSG* m) {
    if (!va) return;
    MEM32(va + 0)  = h2i(m->hwnd);
    MEM32(va + 4)  = m->message;
    MEM32(va + 8)  = (uint32_t)m->wParam;
    MEM32(va + 12) = (uint32_t)m->lParam;
    MEM32(va + 16) = m->time;
    MEM32(va + 20) = (uint32_t)m->pt.x;
    MEM32(va + 24) = (uint32_t)m->pt.y;
}
static void msg_in(uint32_t va, MSG* m) {
    memset(m, 0, sizeof(*m));
    if (!va) return;
    m->hwnd = i2h(MEM32(va + 0));
    m->message = MEM32(va + 4);
    m->wParam = (WPARAM)MEM32(va + 8);
    m->lParam = (LPARAM)(int32_t)MEM32(va + 12);
    m->time = MEM32(va + 16);
    m->pt.x = (LONG)MEM32(va + 20);
    m->pt.y = (LONG)MEM32(va + 24);
}
static void u32_GetMessageA(void) {
    MSG m;
    BOOL r = 0;      /* blocks until a message arrives */
    HWND hw = ARG(1) ? i2h(ARG(1)) : NULL;      /* sampled before releasing */
    uint32_t lo = ARG(2), hi = ARG(3);
    BLOCKING(r = GetMessageA(&m, hw, lo, hi));
    { static unsigned n; if (n++ < 4) fprintf(stderr, "[user32] GetMessageA #%u -> %d\n", n, r); }
    if (r > 0) msg_out(ARG(0), &m);
    RET((uint32_t)r); STDRET(4);
}
static void u32_PeekMessageA(void) {
    MSG m;
    BOOL r = PeekMessageA(&m, ARG(1) ? i2h(ARG(1)) : NULL, ARG(2), ARG(3), ARG(4));
    { static unsigned n; if (n++ < 4) fprintf(stderr, "[user32] PeekMessageA #%u -> %d\n", n, r); }
    if (r) msg_out(ARG(0), &m);
    RET(r ? 1 : 0); STDRET(5);
}
static void u32_TranslateMessage(void) {
    MSG m; msg_in(ARG(0), &m);
    RET(TranslateMessage(&m) ? 1 : 0); STDRET(1);
}
static void u32_DispatchMessageA(void) {
    MSG m; msg_in(ARG(0), &m);
    RET((uint32_t)(int32_t)DispatchMessageA(&m)); STDRET(1);
    { static unsigned n; if (n++ < 4) fprintf(stderr, "[user32] DispatchMessageA #%u msg 0x%04X\n", n, m.message); }
}
extern unsigned g_wait_scale;      /* --waitscale, crt_shims.c */

/*
 * MsgWaitForMultipleObjects, and the reason the game's first section ended.
 *
 * This used to pass nCount = 0 and no handle array -- "with no real worker
 * threads there is nothing else to wait on" -- and to read the timeout from
 * ARG(2), which is bWaitAll, not dwMilliseconds. Both were wrong, and the
 * first one wrong in the worst possible way, because the return value is a
 * position in the handle array:
 *
 *     WAIT_OBJECT_0 + n   a message arrived
 *     WAIT_OBJECT_0 + i   handle i signalled
 *     WAIT_TIMEOUT        neither
 *
 * With nCount = 0 "a message arrived" comes back as WAIT_OBJECT_0 + 0, which
 * is exactly what "handle 0 signalled" looks like. And handle 0, for every
 * Ronin thread, is its stop event: sub_00550F60 ticks the thread on
 * WAIT_TIMEOUT, pumps messages on anything else non-zero, and on plain zero
 * returns false, which its caller answers by clearing [thread+0xC] and
 * retiring the thread -- and a boot process lives exactly as long as its boot
 * thread. So the first stray mouse move ended the section, and the number of
 * frames it survived first (61, 68, 164 in different runs) was just how long it
 * took one message to arrive.
 *
 * Arguments are sampled before the machine is released -- see BLOCKING -- and
 * the handle array is copied, because MEM32 reads target memory that another
 * thread may be inside.
 */
static void u32_MsgWaitForMultipleObjects(void) {
    uint32_t n = ARG(0), pv = ARG(1), all = ARG(2), ms = ARG(3), flags = ARG(4);
    HANDLE h[MAXIMUM_WAIT_OBJECTS];
    if (n > MAXIMUM_WAIT_OBJECTS) n = MAXIMUM_WAIT_OBJECTS;
    for (uint32_t i = 0; i < n; i++) h[i] = i2h(MEM32(pv + i * 4));
    if (ms != INFINITE) {
        uint64_t scaled = (uint64_t)ms * g_wait_scale;
        ms = scaled > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)scaled;
    }
    uint32_t r = 0;
    BLOCKING(r = (uint32_t)MsgWaitForMultipleObjects(n, n ? h : NULL,
                                                    (BOOL)all, ms, flags));
    RET(r); STDRET(5);
}
static void u32_PostThreadMessageA(void) {
    RET(PostThreadMessageA(ARG(0), ARG(1), (WPARAM)ARG(2),
                           (LPARAM)(int32_t)ARG(3)) ? 1 : 0);
    STDRET(4);
}
/*
 * CreateDialogParamA. `ShowLoadingPanel` is the first directive that needs it,
 * and the dialog procedure is lifted code, so it goes through the same
 * trampoline. The template lives in our mapped copy of Focom.exe rather than in
 * a loaded module, so CreateDialogIndirectParam is the only form that can be
 * used -- see imp_LoadBitmapA for the same problem with bitmaps.
 */
static void u32_CreateDialogParamA(void) {
    uint32_t tmpl = ARG(1), proc = ARG(3);
    uint32_t rsz = 0;
    const uint8_t* res = find_resource(5 /* RT_DIALOG */, tmpl, &rsz);
    if (!res) {
        fprintf(stderr, "[user32] CreateDialogParamA: no DIALOG resource %u\n", tmpl);
        RET(0); STDRET(5); return;
    }
    HWND h = CreateDialogIndirectParamA(GetModuleHandleA(NULL),
                                        (LPCDLGTEMPLATE)res,
                                        ARG(2) ? i2h(ARG(2)) : NULL,
                                        (DLGPROC)win_trampoline,
                                        (LPARAM)(int32_t)ARG(4));
    fprintf(stderr, "[user32] CreateDialogParamA(%u) proc=0x%08X -> %p\n",
            tmpl, proc, (void*)h);
    if (!h) { RET(0); STDRET(5); return; }
    win_bind(h, proc);
    RET(h2i(h)); STDRET(5);
}

/* ------------------------------------------------------------- threads

 * The game's startup creates a worker thread and then polls for it: acquire a
 * mutex, check a queue, release, WaitForSingleObject(..., 100), repeat. With the
 * thread not running, that loop never ends, and it is the last thing between
 * here and content being loaded.
 *
 * The obstacle is that the machine state -- g_eax..g_esp, the x87 stack, the
 * control word -- is a set of globals shared by 10.7 million lines of generated
 * C. Two host threads running lifted code would race on every register.
 *
 * The obvious fix, making those globals `__thread`, does not work here: GCC on
 * Windows compiles thread-local access through __emutls_get_address, a function
 * call per access, and the generated code touches registers constantly. It was
 * measured before being ruled out.
 *
 * So: ONE thread runs lifted code at a time, and the switch points are the
 * blocking shims. A thread holds g_mach while it executes, saves the machine
 * state and releases the lock before it blocks for real, and restores its own
 * state after re-acquiring. Nothing mutates the registers except the holder of
 * the lock, so no register is ever read by one thread while another writes it.
 *
 * Win32 TLS (TlsAlloc/TlsGetValue) holds the per-thread saved state, which is
 * touched only at switch points -- a handful of times per millisecond, not per
 * instruction, so emutls's cost does not arise.
 *
 * The ceiling: a thread that never blocks starves the others, because there is
 * no preemption. The game's worker polls with a timeout, which is what makes
 * this work. If a thread turns up that spins instead, it needs a real
 * per-thread register file and the generated code has to change with it.
 *
 * That ceiling was measured TWICE and is not what ends the game's first
 * section. A coarse preemption -- hand the machine over every N function
 * entries if anyone is waiting -- was written and tried at N = 500 through
 * 50,000, first when the game got two threads up and again when RE3D pushed it
 * to four. Neither time did anything change, and the second time the reason
 * was clear: of the four Ronin workers, two carry the load and the other two
 * run 13 and 8 lifted calls each and then park in their own
 * WaitForSingleObject(mutex, 100) queue-consumer loop. That is not starvation,
 * it is an idle worker with nothing queued, and it is the right behaviour. The
 * preemption was deleted rather than left in as dead weight in the hot path.
 *
 * The lesson is about the evidence, not the threading: a thread with no
 * "routine RETURNED" line has not necessarily failed to run, and the way to
 * tell is the per-thread call count in a --calltrace, not the absence of a
 * log line.
 */
#define MACH_STACK_BASE 0x08000000u      /* below the heap, above the image */
#define MACH_STACK_SIZE 0x00100000u      /* 1 MB each, as the main one is */
#define MACH_MAX_THREADS 8

typedef struct {
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp;
    double   st[8];
    int      fp_top;
    uint16_t fpu_cw;
    uint32_t cur_func;
    int      depth;        /* nested claims: only the outermost swaps state */
    uint32_t fs_base;      /* each thread needs its OWN TIB: fs:[0] is the head
                            * of the SEH chain, and MSVC's prologues splice
                            * onto it. Sharing one means each thread unwinds
                            * through the other's frames. */
} mstate;

static CRITICAL_SECTION g_mach;
static volatile long    g_mach_waiters;   /* threads blocked in mach_enter */
static DWORD  g_mach_tls = TLS_OUT_OF_INDEXES;
static long   g_mach_threads;            /* how many simulated stacks handed out */
int           g_no_threads;              /* --nothreads: the old behaviour */

static void mach_save(mstate* m) {
    m->eax = g_eax; m->ecx = g_ecx; m->edx = g_edx; m->ebx = g_ebx;
    m->esi = g_esi; m->edi = g_edi; m->ebp = g_ebp; m->esp = g_esp;
    memcpy(m->st, g_st, sizeof m->st);
    m->fp_top = g_fp_top; m->fpu_cw = g_fpu_cw; m->cur_func = g_cur_func;
    m->fs_base = g_fs_base;
}
static void mach_load(const mstate* m) {
    g_eax = m->eax; g_ecx = m->ecx; g_edx = m->edx; g_ebx = m->ebx;
    g_esi = m->esi; g_edi = m->edi; g_ebp = m->ebp; g_esp = m->esp;
    memcpy(g_st, m->st, sizeof g_st);
    g_fp_top = m->fp_top; g_fpu_cw = m->fpu_cw; g_cur_func = m->cur_func;
    g_fs_base = m->fs_base;
}

void mach_init(void) {
    InitializeCriticalSection(&g_mach);
    g_mach_tls = TlsAlloc();
}

/* Claim the machine for this thread, allocating its saved slot on first use. */
/*
 * Claims can NEST. Lifted code calls DispatchMessageA, Windows calls back into
 * win_trampoline, and the trampoline runs more lifted code -- all on one thread
 * that already owns the machine. A CRITICAL_SECTION lets that through, but
 * reloading the saved state on the inner claim would throw away the live
 * registers, so only the outermost claim swaps.
 */
int mach_depth(void) {
    if (g_mach_tls == TLS_OUT_OF_INDEXES) return -1;
    mstate* m = (mstate*)TlsGetValue(g_mach_tls);
    return m ? m->depth : -1;
}

void mach_enter(void) {
    if (g_mach_tls == TLS_OUT_OF_INDEXES) return;
    InterlockedIncrement(&g_mach_waiters);
    EnterCriticalSection(&g_mach);
    InterlockedDecrement(&g_mach_waiters);
    mstate* m = (mstate*)TlsGetValue(g_mach_tls);
    if (!m) {
        /* First claim by this thread -- the main one, whose state is whatever
         * the runtime already set up. CAPTURE it rather than load: without a
         * slot of its own nothing restored the main thread's registers after a
         * worker ran, and it resumed on the worker's esp. */
        m = (mstate*)calloc(1, sizeof *m);
        if (!m) return;
        mach_save(m);
        m->depth = 1;
        TlsSetValue(g_mach_tls, m);
        return;
    }
    if (m->depth++ == 0) mach_load(m);
}

/* Release it around a real block, saving what this thread was doing. */
void mach_leave(void) {
    if (g_mach_tls == TLS_OUT_OF_INDEXES) return;
    mstate* m = (mstate*)TlsGetValue(g_mach_tls);
    if (m && --m->depth == 0) mach_save(m);
    LeaveCriticalSection(&g_mach);
}

/*
 * Hand the machine over, then take it back.
 *
 * Releasing the lock is not enough. A CRITICAL_SECTION makes no fairness
 * promise, so a thread that leaves and immediately re-enters can win it back
 * before the thread already blocked on it is even scheduled. The game's loading
 * loop calls Sleep(0) once per iteration while the mission process runs on
 * another thread, and that thread starved: the manager reaped it as finished
 * and the game exited having run one frame. Attaching a --calltrace made it go
 * away, which is the signature of a scheduling race.
 *
 * So if anyone was waiting, spin (yielding) until the waiter count drops --
 * proof that one of them got in -- before asking for it back. Bounded, because
 * a waiter that dies or never runs must not hang the yielder.
 */
void mach_yield(uint32_t sleep_ms) {
    long before = g_mach_waiters;
    mach_leave();
    if (sleep_ms) Sleep(sleep_ms);
    for (int spin = 0; before > 0 && g_mach_waiters >= before && spin < 200; spin++)
        if (!SwitchToThread()) Sleep(1);
    mach_enter();
}



typedef struct {
    recomp_func_t fn;
    uint32_t      param;
    uint32_t      stack_top;
    uint32_t      tib;
} thread_arg;

int g_threadtrace = 0;   /* --threadtrace */

static DWORD WINAPI lifted_thread(LPVOID p) {
    thread_arg* a = (thread_arg*)p;
    mstate* m = (mstate*)calloc(1, sizeof *m);
    if (!m) return 1;
    m->esp = a->stack_top;
    m->fpu_cw = 0x037F;
    m->fs_base = a->tib;
    m->depth = 0;                        /* so the first claim LOADS this state */
    TlsSetValue(g_mach_tls, m);

    mach_enter();                        /* loads m, so esp is this stack */
    PUSH32(g_esp, a->param);
    PUSH32(g_esp, RECOMP_RETADDR);
    a->fn();
    uint32_t rc = g_eax;
    fprintf(stderr, "[k32] thread %lu routine RETURNED (rc=%u)\n", GetCurrentThreadId(), rc);
    /* The interesting call path is the one that let the routine finish, and it
     * is gone by the time the process is reaped, so dump the ring here. */
    if (g_threadtrace) recomp_dump_trace("thread routine returned");
    mach_leave();

    free(m);
    free(a);
    return rc;
}

static void k32_CreateThread(void) {
    uint32_t start = ARG(2), param = ARG(3);
    recomp_func_t fn = recomp_lookup(start);
    if (!fn || g_no_threads || g_mach_threads >= MACH_MAX_THREADS) {
        fprintf(stderr, "[k32] CreateThread(start=0x%08X) -- NOT run (%s)\n",
                start, !fn ? "not lifted"
                     : g_no_threads ? "--nothreads" : "too many threads");
        if (ARG(5)) MEM32(ARG(5)) = 1;
        HANDLE h = CreateEventA(NULL, TRUE, TRUE, NULL);   /* signalled */
        RET(h ? h2i(h) : 0); STDRET(6); return;
    }

    /* Each simulated thread gets its own 1 MB of target stack. They sit below
     * the heap so a stray pointer into one is still recognisable by region. */
    long n = InterlockedIncrement(&g_mach_threads) - 1;
    uint32_t base = MACH_STACK_BASE + (uint32_t)n * MACH_STACK_SIZE;
    if (!VirtualAlloc((void*)(uintptr_t)base, MACH_STACK_SIZE,
                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
        fprintf(stderr, "[k32] CreateThread: stack at 0x%08X failed (%lu)\n",
                base, GetLastError());
        RET(0); STDRET(6); return;
    }

    thread_arg* a = (thread_arg*)calloc(1, sizeof *a);
    a->fn = fn;
    a->param = param;
    a->stack_top = base + MACH_STACK_SIZE - 0x100;
    /* Its own TIB, laid out like the main one in recomp_runtime.c. */
    a->tib = crt_alloc(0x1000);
    memset((void*)(uintptr_t)ADDR(a->tib), 0, 0x1000);
    MEM32(a->tib + 0x00) = 0xFFFFFFFFu;              /* SEH: end of chain */
    MEM32(a->tib + 0x04) = base + MACH_STACK_SIZE;   /* stack base */
    MEM32(a->tib + 0x08) = base;                     /* stack limit */
    MEM32(a->tib + 0x18) = a->tib;

    /*
     * CREATE_SUSPENDED has to be honoured. The game creates this thread
     * suspended, finishes building the object the routine works on, and only
     * then resumes it -- started early, the routine runs against a half-built
     * object and faults writing through a member not yet assigned.
     *
     * The real host thread handle is what comes back, so WaitForSingleObject
     * on it signals when the routine returns, the way a thread handle does,
     * and ResumeThread has something to resume.
     */
    DWORD tid = 0;
    DWORD flags = ARG(4) & CREATE_SUSPENDED;
    HANDLE th = CreateThread(NULL, 0, lifted_thread, a, flags, &tid);
    fprintf(stderr, "[k32] CreateThread(start=0x%08X, param=0x%08X)%s"
                    " -> thread %lu, target stack 0x%08X..0x%08X\n",
            start, param, flags ? " suspended" : "", tid,
            base, base + MACH_STACK_SIZE);
    if (ARG(5)) MEM32(ARG(5)) = (uint32_t)tid;
    RET(th ? h2i(th) : 0); STDRET(6);
}
static void k32_ResumeThread(void) {
    HANDLE h = i2h(ARG(0));
    DWORD prev = h ? ResumeThread(h) : 0xFFFFFFFFu;
    fprintf(stderr, "[k32] ResumeThread(h=%u) -> %ld\n", ARG(0), (long)prev);
    RET((uint32_t)prev); STDRET(1);
}



static const struct { const char* name; import_fn_t fn; } g_real[] = {
    { "USER32.dll!LoadBitmapA",            imp_LoadBitmapA },
    { "USER32.dll!RegisterClassA",             u32_RegisterClassA },
    { "USER32.dll!RegisterClassExA",           u32_RegisterClassExA },
    { "USER32.dll!CreateWindowExA",            u32_CreateWindowExA },
    { "USER32.dll!DefWindowProcA",             u32_DefWindowProcA },
    { "USER32.dll!DestroyWindow",              u32_DestroyWindow },
    { "USER32.dll!ShowWindow",                 u32_ShowWindow },
    { "USER32.dll!UpdateWindow",               u32_UpdateWindow },
    { "USER32.dll!IsWindow",                   u32_IsWindow },
    { "USER32.dll!IsIconic",                   u32_IsIconic },
    { "USER32.dll!IsWindowVisible",            u32_IsWindowVisible },
    { "USER32.dll!GetParent",                  u32_GetParent },
    { "USER32.dll!SetParent",                  u32_SetParent },
    { "USER32.dll!SetWindowTextA",             u32_SetWindowTextA },
    { "USER32.dll!SetWindowPos",               u32_SetWindowPos },
    { "USER32.dll!GetWindowLongA",             u32_GetWindowLongA },
    { "USER32.dll!SetWindowLongA",             u32_SetWindowLongA },
    { "USER32.dll!GetClientRect",              u32_GetClientRect },
    { "USER32.dll!ClientToScreen",             u32_ClientToScreen },
    { "USER32.dll!SetCursorPos",               u32_SetCursorPos },
    { "USER32.dll!GetPropA",                   u32_GetPropA },
    { "USER32.dll!SetPropA",                   u32_SetPropA },
    { "USER32.dll!GetWindowPlacement",         u32_GetWindowPlacement },
    { "USER32.dll!SetWindowPlacement",         u32_SetWindowPlacement },
    { "USER32.dll!GetMessageA",                u32_GetMessageA },
    { "USER32.dll!PeekMessageA",               u32_PeekMessageA },
    { "USER32.dll!TranslateMessage",           u32_TranslateMessage },
    { "USER32.dll!DispatchMessageA",           u32_DispatchMessageA },
    { "USER32.dll!MsgWaitForMultipleObjects",  u32_MsgWaitForMultipleObjects },
    { "USER32.dll!PostThreadMessageA",         u32_PostThreadMessageA },
    { "USER32.dll!CreateDialogParamA",         u32_CreateDialogParamA },
    { "KERNEL32.dll!CreateThread",             k32_CreateThread },
    { "KERNEL32.dll!ResumeThread",             k32_ResumeThread },
    { "USER32.dll!GetUpdateRect",          imp_GetUpdateRect },
    { "USER32.dll!BeginPaint",             imp_BeginPaint },
    { "USER32.dll!EndPaint",               imp_EndPaint },
    { "USER32.dll!GetWindowRect",          imp_GetWindowRect },
    { "USER32.dll!GetWindowTextA",         imp_GetWindowTextA },
    { "GDI32.dll!CreateCompatibleDC",      imp_CreateCompatibleDC },
    { "GDI32.dll!SelectObject",            imp_SelectObject },
    { "GDI32.dll!DeleteDC",                imp_DeleteDC },
    { "GDI32.dll!DeleteObject",            imp_DeleteObject },
    { "GDI32.dll!StretchBlt",              imp_StretchBlt },
    { "GDI32.dll!CreateFontA",             imp_CreateFontA },
    { "GDI32.dll!SetBkMode",               imp_SetBkMode },
    { "GDI32.dll!SetTextColor",            imp_SetTextColor },
    { "GDI32.dll!GetTextExtentPoint32A",   imp_GetTextExtentPoint32A },
    { "GDI32.dll!TextOutA",                imp_TextOutA },
};

/* crt_shims.c owns the CRT and KERNEL32 half. */
extern const struct { const char* name; import_fn_t fn; } g_crt_shims[];
extern const unsigned g_crt_shim_count;
/* stl_shims.c owns basic_string and the iostream no-ops. */
extern const struct { const char* name; import_fn_t fn; } g_stl_shims[];
extern const unsigned g_stl_shim_count;

import_fn_t ddraw_static_import(const char* qualified);  /* ddraw_shims.c */

import_fn_t shim_real_import(const char* qualified_name) {
    for (unsigned i = 0; i < sizeof(g_real) / sizeof(g_real[0]); i++)
        if (!strcmp(g_real[i].name, qualified_name)) return g_real[i].fn;
    {   /* the DirectX entry points the game imports statically */
        import_fn_t f = ddraw_static_import(qualified_name);
        if (f) return f;
    }
    for (unsigned i = 0; i < g_crt_shim_count; i++)
        if (!strcmp(g_crt_shims[i].name, qualified_name)) return g_crt_shims[i].fn;
    for (unsigned i = 0; i < g_stl_shim_count; i++)
        if (!strcmp(g_stl_shims[i].name, qualified_name)) return g_stl_shims[i].fn;
    return NULL;
}
