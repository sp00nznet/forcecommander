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

static struct { HWND h; uint32_t proc; } g_winproc[64];
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
    recomp_func_t f = va ? recomp_lookup(va) : NULL;
    if (!f) return DefWindowProcA(h, m, w, l);

    uint32_t save_esp = g_esp, save_fn = g_cur_func;
    uint32_t r = call_lifted_stdcall4(f, h2i(h), m, (uint32_t)w, (uint32_t)l);
    g_cur_func = save_fn;
    if (g_esp != save_esp) {
        fprintf(stderr, "[user32] wndproc 0x%08X msg 0x%04X left esp at "
                        "0x%08X, expected 0x%08X\n", va, m, g_esp, save_esp);
        g_esp = save_esp;
    }
    return (LRESULT)(int32_t)r;
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
    HWND h = CreateWindowExA(ARG(0), cls, title, style,
                             (int)ARG(4), (int)ARG(5), (int)ARG(6), (int)ARG(7),
                             ARG(8) ? i2h(ARG(8)) : NULL, NULL,
                             GetModuleHandleA(NULL), NULL);
    fprintf(stderr, "[user32] CreateWindowExA(\"%s\", \"%s\") -> %p\n",
            cls, title, (void*)h);
    if (!h) { RET(0); STDRET(12); return; }
    uint32_t proc = class_proc(cls);
    if (proc) win_bind(h, proc);
    RET(h2i(h)); STDRET(12);
}
static void u32_DefWindowProcA(void) {
    RET((uint32_t)(int32_t)DefWindowProcA(i2h(ARG(0)), ARG(1),
                                          (WPARAM)ARG(2), (LPARAM)(int32_t)ARG(3)));
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
static void u32_GetWindowLongA(void) { RET((uint32_t)GetWindowLongA(i2h(ARG(0)), (int)ARG(1))); STDRET(2); }
static void u32_SetWindowLongA(void) { RET((uint32_t)SetWindowLongA(i2h(ARG(0)), (int)ARG(1), (LONG)ARG(2))); STDRET(3); }
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
    BOOL r = GetMessageA(&m, ARG(1) ? i2h(ARG(1)) : NULL, ARG(2), ARG(3));
    if (r > 0) msg_out(ARG(0), &m);
    RET((uint32_t)r); STDRET(4);
}
static void u32_PeekMessageA(void) {
    MSG m;
    BOOL r = PeekMessageA(&m, ARG(1) ? i2h(ARG(1)) : NULL, ARG(2), ARG(3), ARG(4));
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
}
static void u32_MsgWaitForMultipleObjects(void) {
    /* The game uses this to idle until input or a handle signals. With no real
     * worker threads there is nothing else to wait on, so honour the timeout
     * against the message queue only. */
    RET((uint32_t)MsgWaitForMultipleObjects(0, NULL, FALSE, ARG(2), ARG(4)));
    STDRET(5);
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

/* ----------------------------------------------------------- threads

 * ponytail: a created thread is NOT run. The handle comes back already
 * signalled so anything that waits on it proceeds.
 *
 * The machine state (g_eax..g_esp, the FPU stack, the simulated stack itself)
 * is one set of globals, so a host thread executing lifted code would race the
 * main one on every register. Running the routine synchronously instead was
 * tried and does not terminate -- the thread InitBase creates is a service
 * loop, not an initialise-and-return worker.
 *
 * The ceiling: anything the game only ever does on that thread never happens.
 * Lifting it properly means making the machine state thread-local and giving
 * each thread its own simulated stack, which is the point to do if a frame
 * turns out to depend on it.
 */
static void k32_CreateThread(void) {
    fprintf(stderr, "[k32] CreateThread(start=0x%08X, param=0x%08X)"
                    " -- not run (single machine state)\n", ARG(2), ARG(3));
    if (ARG(5)) MEM32(ARG(5)) = 1;                     /* any non-zero id */
    HANDLE h = CreateEventA(NULL, TRUE, TRUE, NULL);   /* already signalled */
    RET(h ? h2i(h) : 0); STDRET(6);
}
static void k32_ResumeThread(void) { (void)ARG(0); RET(1); STDRET(1); }


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

import_fn_t shim_real_import(const char* qualified_name) {
    for (unsigned i = 0; i < sizeof(g_real) / sizeof(g_real[0]); i++)
        if (!strcmp(g_real[i].name, qualified_name)) return g_real[i].fn;
    for (unsigned i = 0; i < g_crt_shim_count; i++)
        if (!strcmp(g_crt_shims[i].name, qualified_name)) return g_crt_shims[i].fn;
    for (unsigned i = 0; i < g_stl_shim_count; i++)
        if (!strcmp(g_stl_shims[i].name, qualified_name)) return g_stl_shims[i].fn;
    return NULL;
}
