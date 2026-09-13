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

static void imp_GetWindowRect(void) {
    RECT* r = ARGP(1, RECT);
    RET(GetWindowRect((HWND)i2h(ARG(0)), r)); STDRET(2);
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
static const struct { const char* name; import_fn_t fn; } g_real[] = {
    { "USER32.dll!LoadBitmapA",            imp_LoadBitmapA },
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

import_fn_t shim_real_import(const char* qualified_name) {
    for (unsigned i = 0; i < sizeof(g_real) / sizeof(g_real[0]); i++)
        if (!strcmp(g_real[i].name, qualified_name)) return g_real[i].fn;
    return NULL;
}
