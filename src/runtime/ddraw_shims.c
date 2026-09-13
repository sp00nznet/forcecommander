/*
 * Force Commander - a DirectDraw the recompiled game can actually talk to.
 *
 * The game reaches its renderer through LoadLibrary("DDRAW.DLL") +
 * GetProcAddress("DirectDrawCreate"), then works entirely through COM
 * interface pointers. So this is not an import shim: it has to be an *object
 * model* that lives in the target's address space, because the lifted code
 * loads a vtable pointer out of the object and calls through it.
 *
 * Three pieces make that work:
 *
 *   1. Objects are allocated from the target heap, so `this` is a 32-bit VA the
 *      lifted code can dereference with MEM32.
 *   2. Vtables are arrays of *synthetic* VAs in a reserved range. Nothing is
 *      there; they exist only to be looked up.
 *   3. recomp_lookup_manual() -- a hook the runtime already had and never
 *      used -- maps those synthetic VAs back to host functions. That is the
 *      whole trick, and it is why RECOMP_ICALL through a vtable slot lands in C.
 *
 * COM methods are __stdcall with `this` as the first *stack* argument, so
 * ARG(0) is this and ARG(1) onwards are the declared parameters.
 *
 * Only the surface path is real. RE3D has three renderers and the one being
 * targeted is the software one -- docs/RECON.md found CDD7MemRenderer and
 * CDD7WinScreen, which render into a memory surface and present it in a
 * window -- so what the game needs from DirectDraw is a lockable buffer and a
 * blit, not Direct3D. Everything else answers DD_OK and does nothing.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "recomp_types.h"
#include "imports.h"

uint32_t crt_alloc(uint32_t n);
void*    host_surface(void);
int      host_width(void);
int      host_height(void);
void     host_present(void);
void     host_resize(int w, int h);

#define DD_OK             0
#define DDERR_UNSUPPORTED 0x80004001u
#define E_NOINTERFACE     0x80004002u

/* ---------------------------------------------- synthetic method addresses */

/*
 * A reserved VA range that holds no code. A vtable slot contains one of these,
 * the lifted code calls it, and recomp_lookup_manual turns it back into the C
 * function below. 0x7E000000 is above the image, the heap and the stub module
 * range, and below the 0x7F000000 LoadLibrary hands out.
 */
#define METHOD_BASE  0x7E000000u
#define METHOD_MAX   512

static import_fn_t g_methods[METHOD_MAX];
static const char* g_method_names[METHOD_MAX];
static unsigned    g_method_n;

static uint32_t method(import_fn_t fn, const char* name) {
    if (g_method_n >= METHOD_MAX) return 0;
    g_methods[g_method_n] = fn;
    g_method_names[g_method_n] = name;
    return METHOD_BASE + (g_method_n++) * 4;
}

static int g_trace;      /* FOCOM_TRACE_DD */

/*
 * Called from recomp_lookup_manual, on the indirect-call path.
 *
 * The trace is the whole debugging story for this layer: a COM call sequence
 * says which method returned something the game could not use, and nothing
 * else does. Each name is printed once unless FOCOM_TRACE_DD asks for all of
 * them, so a normal run shows the shape of initialisation without drowning in
 * per-frame Lock/Unlock.
 */
import_fn_t ddraw_lookup_method(uint32_t va) {
    if (va < METHOD_BASE || va >= METHOD_BASE + METHOD_MAX * 4) return NULL;
    uint32_t i = (va - METHOD_BASE) / 4;
    if (i >= g_method_n) return NULL;
    static unsigned char seen[METHOD_MAX];
    if (g_trace || !seen[i]) {
        seen[i] = 1;
        fprintf(stderr, "[com] %s\n", g_method_names[i]);
    }
    return g_methods[i];
}

const char* ddraw_method_name(uint32_t va) {
    if (va < METHOD_BASE || va >= METHOD_BASE + METHOD_MAX * 4) return NULL;
    uint32_t i = (va - METHOD_BASE) / 4;
    return (i < g_method_n) ? g_method_names[i] : NULL;
}

/* ------------------------------------------------------------- objects */

/*
 * Object layout in target memory. +0 is the vtable pointer because that is
 * what the compiler emitted on the other side; the rest is ours.
 *
 *   +0x00  vtable VA
 *   +0x04  refcount
 *   +0x08  kind (1 = IDirectDraw, 2 = surface)
 *   +0x0C  width
 *   +0x10  height
 *   +0x14  bpp
 *   +0x18  pitch
 *   +0x1C  pixel buffer VA
 *   +0x20  caps (DDSCAPS_PRIMARYSURFACE etc.)
 *   +0x24  back-buffer VA (for a flipping chain)
 */
#define O_VTBL(o)   MEM32((o) + 0x00)
#define O_REF(o)    MEM32((o) + 0x04)
#define O_KIND(o)   MEM32((o) + 0x08)
#define O_W(o)      MEM32((o) + 0x0C)
#define O_H(o)      MEM32((o) + 0x10)
#define O_BPP(o)    MEM32((o) + 0x14)
#define O_PITCH(o)  MEM32((o) + 0x18)
#define O_BITS(o)   MEM32((o) + 0x1C)
#define O_CAPS(o)   MEM32((o) + 0x20)
#define O_BACK(o)   MEM32((o) + 0x24)
#define O_SIZE      0x30

#define KIND_DD      1
#define KIND_SURFACE 2

#define DDSCAPS_PRIMARYSURFACE 0x00000200u
#define DDSCAPS_BACKBUFFER     0x00000004u
#define DDSCAPS_FLIP           0x00000010u

static uint32_t g_vtbl_dd, g_vtbl_surf;
static uint32_t g_primary;             /* the surface presented to the window */
static int      g_mode_w = 640, g_mode_h = 480, g_mode_bpp = 16;

static uint32_t obj_new(uint32_t vtbl, uint32_t kind) {
    uint32_t o = crt_alloc(O_SIZE);
    if (!o) return 0;
    for (uint32_t i = 0; i < O_SIZE; i += 4) MEM32(o + i) = 0;
    O_VTBL(o) = vtbl;
    O_REF(o) = 1;
    O_KIND(o) = kind;
    return o;
}

/* ------------------------------------------------------------- present */

/*
 * Copy a surface into the host's 32bpp DIB. The game renders at 16bpp 565 in
 * the software path, which is what the era and RE3D's Bl16 chunk both say, so
 * the conversion lives here rather than asking the game to change format.
 */
static void present_surface(uint32_t s) {
    if (!s || !O_BITS(s)) return;
    uint32_t* dst = (uint32_t*)host_surface();
    if (!dst) return;
    int w = (int)O_W(s), h = (int)O_H(s);
    int hw = host_width(), hh = host_height();
    if (w > hw) w = hw;
    if (h > hh) h = hh;

    if (O_BPP(s) == 16) {
        for (int y = 0; y < h; y++) {
            const uint16_t* src =
                (const uint16_t*)(uintptr_t)ADDR(O_BITS(s) + (uint32_t)y * O_PITCH(s));
            uint32_t* d = dst + (size_t)y * hw;
            for (int x = 0; x < w; x++) {
                uint16_t p = src[x];
                unsigned r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
                d[x] = (uint32_t)(((r * 255 + 15) / 31) << 16 |
                                  ((g * 255 + 31) / 63) << 8 |
                                  ((b * 255 + 15) / 31));
            }
        }
    } else if (O_BPP(s) == 32) {
        for (int y = 0; y < h; y++) {
            const uint32_t* src =
                (const uint32_t*)(uintptr_t)ADDR(O_BITS(s) + (uint32_t)y * O_PITCH(s));
            memcpy(dst + (size_t)y * hw, src, (size_t)w * 4);
        }
    } else if (O_BPP(s) == 8) {
        /* No palette plumbing yet: show intensity so something is visible
         * rather than nothing. ponytail: wire SetPalette when a 8bpp mode
         * actually gets used. */
        for (int y = 0; y < h; y++) {
            const uint8_t* src =
                (const uint8_t*)(uintptr_t)ADDR(O_BITS(s) + (uint32_t)y * O_PITCH(s));
            uint32_t* d = dst + (size_t)y * hw;
            for (int x = 0; x < w; x++)
                d[x] = (uint32_t)(src[x] * 0x010101u);
        }
    }
    host_present();
}

/* ------------------------------------------------------ IUnknown, shared */

/*
 * QueryInterface has to read the GUID, and answering yes to everything is the
 * trap. RECON.md found GUIDs for DirectDraw 2/4/7 and Direct3D 2/3 in .rdata,
 * so the game probes for a hardware renderer; accepting IID_IDirect3D hands it
 * an object whose vtable has surface methods at Direct3D slots, and the first
 * call goes somewhere arbitrary.
 *
 * Refusing Direct3D is not a limitation, it is the point. RE3D falls back
 * through Direct3D hardware, Direct3D RGB, then DirectDraw memory-lock, and the
 * memory-lock renderer (CDD7MemRenderer) is the one implemented here. A clean
 * E_NOINTERFACE is how the game is told to take it.
 *
 * The first dword of the GUID identifies each one uniquely, so that is all that
 * is compared.
 */
static void m_QueryInterface(void) {
    uint32_t o = ARG(0), riid = ARG(1), ppv = ARG(2);
    uint32_t g = riid ? MEM32(riid) : 0;

    int is_dd = (g == 0x6C14DB80u ||   /* IID_IDirectDraw  */
                 g == 0xB3A6F3E0u ||   /* IID_IDirectDraw2 */
                 g == 0x9C59509Au ||   /* IID_IDirectDraw4 */
                 g == 0x15E65EC0u);    /* IID_IDirectDraw7 */
    int is_surf = (g == 0x6C14DB81u || /* IID_IDirectDrawSurface  */
                   g == 0x57805885u || /* ...Surface2 */
                   g == 0xDA044E00u || /* ...Surface3 */
                   g == 0x0B2B8630u || /* ...Surface4 */
                   g == 0x06675A80u);  /* ...Surface7 */
    int is_d3d = (g == 0x3BBA0080u ||  /* IID_IDirect3D  */
                  g == 0x6AAE1EC1u ||  /* IID_IDirect3D2 */
                  g == 0xBB223240u ||  /* IID_IDirect3D3 */
                  g == 0xF5049E77u);   /* IID_IDirect3D7 */

    if (is_d3d || (!is_dd && !is_surf)) {
        if (ppv) MEM32(ppv) = 0;
        fprintf(stderr, "[com] QueryInterface {%08X-...} -> E_NOINTERFACE%s\n",
                g, is_d3d ? "  (Direct3D refused: use the software path)" : "");
        RET(E_NOINTERFACE); STDRET(3);
        return;
    }
    /* The version differences are appended methods, and the vtables below are
     * padded through v7, so one object serves every version of its interface. */
    fprintf(stderr, "[com] QueryInterface {%08X-...} -> accepted (%s)\n",
            g, is_dd ? "IDirectDraw" : "Surface");
    if (ppv) MEM32(ppv) = o;
    O_REF(o)++;
    RET(DD_OK); STDRET(3);
}
static void m_AddRef(void)  { O_REF(ARG(0))++; RET(O_REF(ARG(0))); STDRET(1); }
static void m_Release(void) {
    uint32_t o = ARG(0);
    if (O_REF(o)) O_REF(o)--;
    RET(O_REF(o)); STDRET(1);
}

/* --------------------------------------------------------- IDirectDraw */

static void dd_SetCooperativeLevel(void) {
    if (g_trace) fprintf(stderr, "[dd] SetCooperativeLevel(flags=0x%X)\n", ARG(2));
    RET(DD_OK); STDRET(3);
}

static void dd_SetDisplayMode(void) {
    g_mode_w = (int)ARG(1);
    g_mode_h = (int)ARG(2);
    g_mode_bpp = (int)ARG(3);
    fprintf(stderr, "[dd] SetDisplayMode %dx%d %dbpp\n",
            g_mode_w, g_mode_h, g_mode_bpp);
    host_resize(g_mode_w, g_mode_h);
    RET(DD_OK); STDRET(4);
}

static void dd_RestoreDisplayMode(void) { RET(DD_OK); STDRET(1); }
static void dd_Compact(void)            { RET(DD_OK); STDRET(1); }
static void dd_FlipToGDISurface(void)   { RET(DD_OK); STDRET(1); }
static void dd_WaitForVB(void)          { RET(DD_OK); STDRET(3); }

static void dd_GetDisplayMode(void) {
    /* DDSURFACEDESC: dwSize, dwFlags, dwHeight, dwWidth, lPitch, ... and the
     * DDPIXELFORMAT at +0x48. */
    uint32_t d = ARG(1);
    if (d) {
        MEM32(d + 0x08) = (uint32_t)g_mode_h;
        MEM32(d + 0x0C) = (uint32_t)g_mode_w;
        MEM32(d + 0x10) = (uint32_t)(g_mode_w * (g_mode_bpp / 8));
        MEM32(d + 0x48) = 32;                 /* DDPIXELFORMAT.dwSize */
        MEM32(d + 0x4C) = 0x40;               /* DDPF_RGB */
        MEM32(d + 0x54) = (uint32_t)g_mode_bpp;
        if (g_mode_bpp == 16) {
            MEM32(d + 0x58) = 0xF800; MEM32(d + 0x5C) = 0x07E0;
            MEM32(d + 0x60) = 0x001F;
        } else {
            MEM32(d + 0x58) = 0x00FF0000; MEM32(d + 0x5C) = 0x0000FF00;
            MEM32(d + 0x60) = 0x000000FF;
        }
    }
    RET(DD_OK); STDRET(2);
}

/*
 * GetCaps, filled in properly. The near-empty version was reporting
 * dwVidMemTotal = 0 and dwVidMemFree = 0, and a device with no video memory is
 * a device the game cannot allocate a surface on -- so it discarded the
 * enumerated device and exited.
 *
 * DDCAPS offsets, from ddraw.h's DDCAPS_DX7: dwSize 0x00, dwCaps 0x04,
 * dwCaps2 0x08, dwCKeyCaps 0x0C, dwFXCaps 0x10, then bit-depth fields, and
 * dwVidMemTotal/dwVidMemFree at 0x3C/0x40 -- NOT at 0x54, which is where a
 * reading of the older DDCAPS puts them.
 *
 * DDCAPS_3D is deliberately not claimed. Claiming it invites the Direct3D
 * calls that QueryInterface refuses, and the software renderer is the target.
 */
#define DDCAPS_BLT            0x00000040u
#define DDCAPS_BLTQUEUE       0x00000080u
#define DDCAPS_BLTFOURCC      0x00000100u
#define DDCAPS_BLTSTRETCH     0x00000200u
#define DDCAPS_GDI            0x00000400u
#define DDCAPS_CANBLTSYSMEM   0x00000800u
#define DDCAPS_COLORKEY       0x00000008u
#define DDCAPS_CANCLIP        0x00000010u
#define DDCAPS_CANCLIPSTRETCHED 0x00000020u
#define DDCAPS_PALETTE        0x00004000u
#define DDCAPS_BLTCOLORFILL   0x04000000u
#define DDCAPS2_CANRENDERWINDOWED 0x00080000u
#define DDCAPS2_WIDESURFACES      0x00001000u

static void fill_caps(uint32_t c) {
    if (!c) return;
    for (uint32_t i = 0; i < 380; i += 4) MEM32(c + i) = 0;
    MEM32(c + 0x00) = 380;
    MEM32(c + 0x04) = DDCAPS_BLT | DDCAPS_BLTQUEUE | DDCAPS_BLTFOURCC |
                      DDCAPS_BLTSTRETCH | DDCAPS_GDI | DDCAPS_CANBLTSYSMEM |
                      DDCAPS_COLORKEY | DDCAPS_CANCLIP |
                      DDCAPS_CANCLIPSTRETCHED | DDCAPS_PALETTE |
                      DDCAPS_BLTCOLORFILL;
    MEM32(c + 0x08) = DDCAPS2_CANRENDERWINDOWED | DDCAPS2_WIDESURFACES;
    MEM32(c + 0x0C) = 0x00000001u;      /* DDCKEYCAPS_DESTBLT */
    MEM32(c + 0x18) = 0x00000001u;      /* DDPCAPS_8BIT */
    MEM32(c + 0x3C) = 64u << 20;        /* dwVidMemTotal */
    MEM32(c + 0x40) = 64u << 20;        /* dwVidMemFree  */
}

static void dd_GetCaps(void) {
    fill_caps(ARG(1));                   /* driver caps */
    fill_caps(ARG(2));                   /* HEL caps    */
    fprintf(stderr, "[dd] GetCaps -> blt/stretch/gdi, 64 MB video memory\n");
    RET(DD_OK); STDRET(3);
}

static void dd_GetMonitorFrequency(void) {
    if (ARG(1)) MEM32(ARG(1)) = 60;
    RET(DD_OK); STDRET(2);
}
static void dd_GetScanLine(void) {
    if (ARG(1)) MEM32(ARG(1)) = 0;
    RET(DD_OK); STDRET(2);
}
static void dd_GetVBStatus(void) {
    if (ARG(1)) MEM32(ARG(1)) = 0;
    RET(DD_OK); STDRET(2);
}

static uint32_t make_surface(uint32_t w, uint32_t h, uint32_t bpp, uint32_t caps) {
    uint32_t s = obj_new(g_vtbl_surf, KIND_SURFACE);
    if (!s) return 0;
    if (!bpp) bpp = (uint32_t)g_mode_bpp;
    uint32_t pitch = (w * (bpp / 8) + 3u) & ~3u;
    O_W(s) = w; O_H(s) = h; O_BPP(s) = bpp;
    O_PITCH(s) = pitch;
    O_BITS(s) = crt_alloc(pitch * h + 16);
    O_CAPS(s) = caps;
    if (O_BITS(s)) memset((void*)(uintptr_t)ADDR(O_BITS(s)), 0, pitch * h);
    return s;
}

static void dd_CreateSurface(void) {
    uint32_t desc = ARG(1), pps = ARG(2);
    uint32_t flags = desc ? MEM32(desc + 0x04) : 0;
    uint32_t h = desc ? MEM32(desc + 0x08) : 0;
    uint32_t w = desc ? MEM32(desc + 0x0C) : 0;
    uint32_t caps = desc ? MEM32(desc + 0x68) : 0;   /* DDSURFACEDESC.ddsCaps */
    uint32_t backs = desc ? MEM32(desc + 0x14) : 0;  /* dwBackBufferCount */

    if (!w || !h) { w = (uint32_t)g_mode_w; h = (uint32_t)g_mode_h; }

    uint32_t bpp = 0;
    if (desc && MEM32(desc + 0x48)) bpp = MEM32(desc + 0x54);

    uint32_t s = make_surface(w, h, bpp, caps);
    if (!s) { RET(DDERR_UNSUPPORTED); STDRET(4); return; }

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        g_primary = s;
        /* A flipping primary gets a back buffer, and the game renders there. */
        if (backs || (caps & DDSCAPS_FLIP)) {
            uint32_t b = make_surface(w, h, O_BPP(s), DDSCAPS_BACKBUFFER);
            O_BACK(s) = b;
        }
    }
    if (pps) MEM32(pps) = s;
    fprintf(stderr, "[dd] CreateSurface %ux%u %ubpp caps=0x%X flags=0x%X -> "
                    "0x%08X%s\n", w, h, O_BPP(s), caps, flags, s,
            (caps & DDSCAPS_PRIMARYSURFACE) ? " (primary)" : "");
    RET(DD_OK); STDRET(4);
}

static void dd_CreateClipper(void) {
    /* A clipper the game can hold and call Release on. Clipping is the
     * window's job here, so it does nothing else. */
    uint32_t c = obj_new(g_vtbl_surf, KIND_SURFACE);
    if (ARG(2)) MEM32(ARG(2)) = c;
    RET(DD_OK); STDRET(4);
}
static void dd_CreatePalette(void) {
    uint32_t p = obj_new(g_vtbl_surf, KIND_SURFACE);
    if (ARG(3)) MEM32(ARG(3)) = p;
    RET(DD_OK); STDRET(5);
}
static void dd_DuplicateSurface(void) {
    uint32_t src = ARG(1);
    uint32_t s = make_surface(O_W(src), O_H(src), O_BPP(src), 0);
    if (ARG(2)) MEM32(ARG(2)) = s;
    RET(DD_OK); STDRET(3);
}
static void dd_GetGDISurface(void) {
    if (ARG(1)) MEM32(ARG(1)) = g_primary;
    RET(DD_OK); STDRET(2);
}
static void dd_GetFourCC(void) {
    if (ARG(1)) MEM32(ARG(1)) = 0;
    RET(DD_OK); STDRET(3);
}
static void dd_Initialize(void)  { RET(DD_OK); STDRET(2); }

static void dd_GetAvailableVidMem(void) {
    /* 64 MB free. Reporting zero makes the game decide it cannot allocate
     * surfaces and give up before it draws anything. */
    if (ARG(2)) MEM32(ARG(2)) = 64u << 20;
    if (ARG(3)) MEM32(ARG(3)) = 64u << 20;
    RET(DD_OK); STDRET(4);
}

/*
 * EnumDisplayModes has to call back INTO lifted code, which is the other
 * direction across the boundary and the only place here that needs it. One
 * mode is offered: the one that is implemented.
 */
static void dd_EnumDisplayModes(void) {
    uint32_t ctx = ARG(3), cb = ARG(4);
    if (cb) {
        uint32_t d = crt_alloc(0x6C);
        for (uint32_t i = 0; i < 0x6C; i += 4) MEM32(d + i) = 0;
        MEM32(d + 0x00) = 0x6C;
        MEM32(d + 0x04) = 0x0000000F;
        MEM32(d + 0x08) = (uint32_t)g_mode_h;
        MEM32(d + 0x0C) = (uint32_t)g_mode_w;
        MEM32(d + 0x10) = (uint32_t)(g_mode_w * (g_mode_bpp / 8));
        MEM32(d + 0x48) = 32;
        MEM32(d + 0x4C) = 0x40;
        MEM32(d + 0x54) = (uint32_t)g_mode_bpp;
        MEM32(d + 0x58) = 0xF800; MEM32(d + 0x5C) = 0x07E0; MEM32(d + 0x60) = 0x001F;

        recomp_func_t f = recomp_lookup(cb);
        if (f) {
            PUSH32(g_esp, ctx);
            PUSH32(g_esp, d);
            PUSH32(g_esp, RECOMP_RETADDR);
            f();                      /* the callback's `ret 8` balances it */
        }
    }
    RET(DD_OK); STDRET(5);
}
static void dd_EnumSurfaces(void) { RET(DD_OK); STDRET(5); }

/* ------------------------------------------------ IDirectDrawSurface */

static void sf_Lock(void) {
    uint32_t s = ARG(0), desc = ARG(2);
    if (desc) {
        MEM32(desc + 0x00) = 0x6C;
        MEM32(desc + 0x04) = 0x0000100F;          /* incl. DDSD_LPSURFACE */
        MEM32(desc + 0x08) = O_H(s);
        MEM32(desc + 0x0C) = O_W(s);
        MEM32(desc + 0x10) = O_PITCH(s);
        MEM32(desc + 0x24) = O_BITS(s);           /* lpSurface */
        MEM32(desc + 0x48) = 32;
        MEM32(desc + 0x4C) = 0x40;
        MEM32(desc + 0x54) = O_BPP(s);
        if (O_BPP(s) == 16) {
            MEM32(desc + 0x58) = 0xF800; MEM32(desc + 0x5C) = 0x07E0;
            MEM32(desc + 0x60) = 0x001F;
        } else {
            MEM32(desc + 0x58) = 0x00FF0000; MEM32(desc + 0x5C) = 0x0000FF00;
            MEM32(desc + 0x60) = 0x000000FF;
        }
    }
    RET(DD_OK); STDRET(5);
}

static void sf_Unlock(void) {
    uint32_t s = ARG(0);
    if (s == g_primary || O_CAPS(s) & DDSCAPS_PRIMARYSURFACE) present_surface(s);
    RET(DD_OK); STDRET(2);
}

static void sf_Flip(void) {
    uint32_t s = ARG(0);
    uint32_t b = O_BACK(s);
    /* Present what the game drew, then swap the buffers so the next frame
     * renders into the one just shown. */
    present_surface(b ? b : s);
    if (b) {
        uint32_t bits = O_BITS(s);
        O_BITS(s) = O_BITS(b);
        O_BITS(b) = bits;
    }
    RET(DD_OK); STDRET(3);
}

static void blit(uint32_t dst, int dx, int dy, uint32_t src,
                 int sx, int sy, int w, int h) {
    if (!dst || !src || !O_BITS(dst) || !O_BITS(src)) return;
    if (O_BPP(dst) != O_BPP(src)) return;
    int bpp = (int)O_BPP(dst) / 8;
    for (int y = 0; y < h; y++) {
        if (dy + y < 0 || dy + y >= (int)O_H(dst)) continue;
        if (sy + y < 0 || sy + y >= (int)O_H(src)) continue;
        uint8_t* d = (uint8_t*)(uintptr_t)ADDR(O_BITS(dst)
                       + (uint32_t)(dy + y) * O_PITCH(dst) + (uint32_t)dx * bpp);
        const uint8_t* s = (const uint8_t*)(uintptr_t)ADDR(O_BITS(src)
                       + (uint32_t)(sy + y) * O_PITCH(src) + (uint32_t)sx * bpp);
        memcpy(d, s, (size_t)w * bpp);
    }
}

static void sf_Blt(void) {
    uint32_t dst = ARG(0), dr = ARG(1), src = ARG(2), sr = ARG(3);
    int dx = 0, dy = 0, sx = 0, sy = 0;
    int w = (int)O_W(dst), h = (int)O_H(dst);
    if (dr) { dx = (int)MEM32(dr); dy = (int)MEM32(dr + 4);
              w = (int)MEM32(dr + 8) - dx; h = (int)MEM32(dr + 12) - dy; }
    if (sr) { sx = (int)MEM32(sr); sy = (int)MEM32(sr + 4);
              int sw = (int)MEM32(sr + 8) - sx, sh = (int)MEM32(sr + 12) - sy;
              if (sw < w) w = sw;
              if (sh < h) h = sh; }
    if (src) blit(dst, dx, dy, src, sx, sy, w, h);
    if (dst == g_primary) present_surface(dst);
    RET(DD_OK); STDRET(6);
}

static void sf_BltFast(void) {
    uint32_t dst = ARG(0);
    int dx = (int)ARG(1), dy = (int)ARG(2);
    uint32_t src = ARG(3), sr = ARG(4);
    int sx = 0, sy = 0, w = src ? (int)O_W(src) : 0, h = src ? (int)O_H(src) : 0;
    if (sr) { sx = (int)MEM32(sr); sy = (int)MEM32(sr + 4);
              w = (int)MEM32(sr + 8) - sx; h = (int)MEM32(sr + 12) - sy; }
    if (src) blit(dst, dx, dy, src, sx, sy, w, h);
    if (dst == g_primary) present_surface(dst);
    RET(DD_OK); STDRET(6);
}

static void sf_GetSurfaceDesc(void) {
    uint32_t s = ARG(0), d = ARG(1);
    if (d) {
        MEM32(d + 0x00) = 0x6C;
        MEM32(d + 0x04) = 0x0000100F;
        MEM32(d + 0x08) = O_H(s);
        MEM32(d + 0x0C) = O_W(s);
        MEM32(d + 0x10) = O_PITCH(s);
        MEM32(d + 0x24) = O_BITS(s);
        MEM32(d + 0x48) = 32;
        MEM32(d + 0x4C) = 0x40;
        MEM32(d + 0x54) = O_BPP(s);
        if (O_BPP(s) == 16) {
            MEM32(d + 0x58) = 0xF800; MEM32(d + 0x5C) = 0x07E0;
            MEM32(d + 0x60) = 0x001F;
        } else {
            MEM32(d + 0x58) = 0x00FF0000; MEM32(d + 0x5C) = 0x0000FF00;
            MEM32(d + 0x60) = 0x000000FF;
        }
        MEM32(d + 0x68) = O_CAPS(s);
    }
    RET(DD_OK); STDRET(2);
}

static void sf_GetPixelFormat(void) {
    uint32_t s = ARG(0), p = ARG(1);
    if (p) {
        MEM32(p + 0x00) = 32;
        MEM32(p + 0x04) = 0x40;
        MEM32(p + 0x0C) = O_BPP(s);
        if (O_BPP(s) == 16) {
            MEM32(p + 0x10) = 0xF800; MEM32(p + 0x14) = 0x07E0;
            MEM32(p + 0x18) = 0x001F;
        } else {
            MEM32(p + 0x10) = 0x00FF0000; MEM32(p + 0x14) = 0x0000FF00;
            MEM32(p + 0x18) = 0x000000FF;
        }
    }
    RET(DD_OK); STDRET(2);
}

static void sf_GetCaps(void) {
    if (ARG(1)) MEM32(ARG(1)) = O_CAPS(ARG(0));
    RET(DD_OK); STDRET(2);
}

static void sf_GetAttachedSurface(void) {
    uint32_t s = ARG(0);
    if (ARG(2)) MEM32(ARG(2)) = O_BACK(s) ? O_BACK(s) : s;
    RET(DD_OK); STDRET(3);
}

static void sf_IsLost(void)    { RET(DD_OK); STDRET(1); }
static void sf_Restore(void)   { RET(DD_OK); STDRET(1); }
static void sf_ok1(void)       { RET(DD_OK); STDRET(1); }
static void sf_ok2(void)       { RET(DD_OK); STDRET(2); }
static void sf_ok3(void)       { RET(DD_OK); STDRET(3); }
static void sf_ok4(void)       { RET(DD_OK); STDRET(4); }
static void sf_ok5(void)       { RET(DD_OK); STDRET(5); }
static void sf_ok6(void)       { RET(DD_OK); STDRET(6); }

static void sf_GetDC(void) {
    /* No GDI on a target-memory surface. Refusing is correct and the game has
     * a non-GDI path; handing back a bogus HDC would fault inside GDI32. */
    if (ARG(1)) MEM32(ARG(1)) = 0;
    RET(DDERR_UNSUPPORTED); STDRET(2);
}

/* ------------------------------------------------------------- vtables */

typedef struct { import_fn_t fn; const char* nm; } vtent_t;

static uint32_t build_vtable(const vtent_t* m, unsigned n) {
    uint32_t v = crt_alloc(n * 4 + 4);
    for (unsigned i = 0; i < n; i++)
        MEM32(v + i * 4) = method(m[i].fn, m[i].nm);
    return v;
}

void ddraw_init(void) {
    g_trace = getenv("FOCOM_TRACE_DD") != NULL;

    /* IDirectDraw, in declaration order. Getting the order wrong calls the
     * wrong method with the wrong argument count, so the order IS the contract
     * and it is the one from ddraw.h. */
    static const vtent_t dd[] = {
        {m_QueryInterface,        "IDirectDraw::QueryInterface"},
        {m_AddRef,                "IDirectDraw::AddRef"},
        {m_Release,               "IDirectDraw::Release"},
        {dd_Compact,              "IDirectDraw::Compact"},
        {dd_CreateClipper,        "IDirectDraw::CreateClipper"},
        {dd_CreatePalette,        "IDirectDraw::CreatePalette"},
        {dd_CreateSurface,        "IDirectDraw::CreateSurface"},
        {dd_DuplicateSurface,     "IDirectDraw::DuplicateSurface"},
        {dd_EnumDisplayModes,     "IDirectDraw::EnumDisplayModes"},
        {dd_EnumSurfaces,         "IDirectDraw::EnumSurfaces"},
        {dd_FlipToGDISurface,     "IDirectDraw::FlipToGDISurface"},
        {dd_GetCaps,              "IDirectDraw::GetCaps"},
        {dd_GetDisplayMode,       "IDirectDraw::GetDisplayMode"},
        {dd_GetFourCC,            "IDirectDraw::GetFourCCCodes"},
        {dd_GetGDISurface,        "IDirectDraw::GetGDISurface"},
        {dd_GetMonitorFrequency,  "IDirectDraw::GetMonitorFrequency"},
        {dd_GetScanLine,          "IDirectDraw::GetScanLine"},
        {dd_GetVBStatus,          "IDirectDraw::GetVerticalBlankStatus"},
        {dd_Initialize,           "IDirectDraw::Initialize"},
        {dd_RestoreDisplayMode,   "IDirectDraw::RestoreDisplayMode"},
        {dd_SetCooperativeLevel,  "IDirectDraw::SetCooperativeLevel"},
        {dd_SetDisplayMode,       "IDirectDraw::SetDisplayMode"},
        {dd_WaitForVB,            "IDirectDraw::WaitForVerticalBlank"},
        /* v2 and later append; padded so one object serves every version. */
        {dd_GetAvailableVidMem,   "IDirectDraw2::GetAvailableVidMem"},
        {sf_ok3,                  "IDirectDraw4::GetSurfaceFromDC"},
        {sf_ok1,                  "IDirectDraw4::RestoreAllSurfaces"},
        {sf_ok1,                  "IDirectDraw4::TestCooperativeLevel"},
        {sf_ok3,                  "IDirectDraw4::GetDeviceIdentifier"},
        {sf_ok3,                  "IDirectDraw7::StartModeTest"},
        {sf_ok2,                  "IDirectDraw7::EvaluateMode"},
    };
    static const vtent_t sf[] = {
        {m_QueryInterface,        "Surface::QueryInterface"},
        {m_AddRef,                "Surface::AddRef"},
        {m_Release,               "Surface::Release"},
        {sf_ok2,                  "Surface::AddAttachedSurface"},
        {sf_ok2,                  "Surface::AddOverlayDirtyRect"},
        {sf_Blt,                  "Surface::Blt"},
        {sf_ok4,                  "Surface::BltBatch"},
        {sf_BltFast,              "Surface::BltFast"},
        {sf_ok2,                  "Surface::DeleteAttachedSurface"},
        {sf_ok3,                  "Surface::EnumAttachedSurfaces"},
        {sf_ok4,                  "Surface::EnumOverlayZOrders"},
        {sf_Flip,                 "Surface::Flip"},
        {sf_GetAttachedSurface,   "Surface::GetAttachedSurface"},
        {sf_ok2,                  "Surface::GetBltStatus"},
        {sf_GetCaps,              "Surface::GetCaps"},
        {sf_ok2,                  "Surface::GetClipper"},
        {sf_ok3,                  "Surface::GetColorKey"},
        {sf_GetDC,                "Surface::GetDC"},
        {sf_ok2,                  "Surface::GetFlipStatus"},
        {sf_ok3,                  "Surface::GetOverlayPosition"},
        {sf_ok2,                  "Surface::GetPalette"},
        {sf_GetPixelFormat,       "Surface::GetPixelFormat"},
        {sf_GetSurfaceDesc,       "Surface::GetSurfaceDesc"},
        {sf_ok3,                  "Surface::Initialize"},
        {sf_IsLost,               "Surface::IsLost"},
        {sf_Lock,                 "Surface::Lock"},
        {sf_ok2,                  "Surface::ReleaseDC"},
        {sf_Restore,              "Surface::Restore"},
        {sf_ok2,                  "Surface::SetClipper"},
        {sf_ok3,                  "Surface::SetColorKey"},
        {sf_ok3,                  "Surface::SetOverlayPosition"},
        {sf_ok2,                  "Surface::SetPalette"},
        {sf_ok2,                  "Surface::Unlock"},     /* replaced below */
        {sf_ok6,                  "Surface::UpdateOverlay"},
        {sf_ok2,                  "Surface::UpdateOverlayDisplay"},
        {sf_ok3,                  "Surface::UpdateOverlayZOrder"},
        /* v2..v7 append these; stubs, but present so a v7 pointer is callable. */
        {sf_ok2,                  "Surface2::GetDDInterface"},
        {sf_ok1,                  "Surface2::PageLock"},
        {sf_ok1,                  "Surface2::PageUnlock"},
        {sf_ok3,                  "Surface3::SetSurfaceDesc"},
        {sf_ok5,                  "Surface4::SetPrivateData"},
        {sf_ok4,                  "Surface4::GetPrivateData"},
        {sf_ok2,                  "Surface4::FreePrivateData"},
        {sf_ok2,                  "Surface4::GetUniquenessValue"},
        {sf_ok1,                  "Surface4::ChangeUniquenessValue"},
        {sf_ok2,                  "Surface7::SetPriority"},
        {sf_ok2,                  "Surface7::GetPriority"},
        {sf_ok2,                  "Surface7::SetLOD"},
        {sf_ok2,                  "Surface7::GetLOD"},
    };

    g_vtbl_dd = build_vtable(dd, sizeof(dd) / sizeof(dd[0]));
    g_vtbl_surf = build_vtable(sf, sizeof(sf) / sizeof(sf[0]));
    /* Unlock is slot 32 and needs the real body; the table above keeps the
     * ordering readable. */
    MEM32(g_vtbl_surf + 32 * 4) = method(sf_Unlock, "Surface::Unlock");

    printf("  DirectDraw: %u methods, IDirectDraw vtbl 0x%08X, "
           "surface vtbl 0x%08X\n", g_method_n, g_vtbl_dd, g_vtbl_surf);
}


/* ------------------------------------------------------- DirectInput

 * Same mechanism, and deliberately a device that reports "nothing pressed"
 * rather than no device at all: the game creates keyboard and mouse up front
 * and treats failure as fatal, so refusing here stops startup exactly the way
 * a missing DirectDrawCreate did.
 */
#define DI_OK 0

static uint32_t g_vtbl_di, g_vtbl_didev;

static void di_CreateDevice(void) {
    uint32_t o = obj_new(g_vtbl_didev, KIND_DD);
    if (ARG(2)) MEM32(ARG(2)) = o;
    if (g_trace) fprintf(stderr, "[di] CreateDevice -> 0x%08X\n", o);
    RET(DI_OK); STDRET(4);
}
static void di_EnumDevices(void)      { RET(DI_OK); STDRET(5); }
static void di_GetDeviceStatus(void)  { RET(DI_OK); STDRET(2); }
static void di_RunControlPanel(void)  { RET(DI_OK); STDRET(3); }
static void di_Initialize(void)       { RET(DI_OK); STDRET(3); }

static void dev_GetCapabilities(void) {
    /* DIDEVCAPS: dwSize, dwFlags, dwDevType, then axis/button/POV counts. */
    uint32_t c = ARG(1);
    if (c) {
        MEM32(c + 0x00) = 0x44;
        MEM32(c + 0x04) = 0x00000001u;   /* DIDC_ATTACHED */
        MEM32(c + 0x08) = 0x00000012u;   /* keyboard */
        MEM32(c + 0x0C) = 0;             /* axes */
        MEM32(c + 0x10) = 256;           /* buttons */
        MEM32(c + 0x14) = 0;             /* POVs */
    }
    RET(DI_OK); STDRET(2);
}

static void dev_GetDeviceState(void) {
    /* No input: zero the buffer. A device that answers cleanly with nothing
     * pressed is what lets the game run its own loop. */
    uint32_t n = ARG(1), p = ARG(2);
    if (p && n && n < 0x10000)
        memset((void*)(uintptr_t)ADDR(p), 0, n);
    RET(DI_OK); STDRET(3);
}

static void dev_GetDeviceData(void) {
    /* Buffered mode: report zero events by writing 0 back through pdwInOut. */
    if (ARG(3)) MEM32(ARG(3)) = 0;
    RET(DI_OK); STDRET(5);
}

static void dev_ok1(void) { RET(DI_OK); STDRET(1); }
static void dev_ok2(void) { RET(DI_OK); STDRET(2); }
static void dev_ok3(void) { RET(DI_OK); STDRET(3); }
static void dev_ok4(void) { RET(DI_OK); STDRET(4); }
static void dev_ok5(void) { RET(DI_OK); STDRET(5); }

static void dinput_DirectInputCreateA(void) {
    uint32_t o = obj_new(g_vtbl_di, KIND_DD);
    if (ARG(2)) MEM32(ARG(2)) = o;
    fprintf(stderr, "[di] DirectInputCreateA -> 0x%08X\n", o);
    RET(o ? DI_OK : DDERR_UNSUPPORTED); STDRET(4);
}

static void dinput_init(void) {
    static const vtent_t di[] = {
        {m_QueryInterface,   "IDirectInput::QueryInterface"},
        {m_AddRef,           "IDirectInput::AddRef"},
        {m_Release,          "IDirectInput::Release"},
        {di_CreateDevice,    "IDirectInput::CreateDevice"},
        {di_EnumDevices,     "IDirectInput::EnumDevices"},
        {di_GetDeviceStatus, "IDirectInput::GetDeviceStatus"},
        {di_RunControlPanel, "IDirectInput::RunControlPanel"},
        {di_Initialize,      "IDirectInput::Initialize"},
    };
    static const vtent_t dev[] = {
        {m_QueryInterface,      "Device::QueryInterface"},
        {m_AddRef,              "Device::AddRef"},
        {m_Release,             "Device::Release"},
        {dev_GetCapabilities,   "Device::GetCapabilities"},
        {dev_ok4,               "Device::EnumObjects"},
        {dev_ok3,               "Device::GetProperty"},
        {dev_ok3,               "Device::SetProperty"},
        {dev_ok1,               "Device::Acquire"},
        {dev_ok1,               "Device::Unacquire"},
        {dev_GetDeviceState,    "Device::GetDeviceState"},
        {dev_GetDeviceData,     "Device::GetDeviceData"},
        {dev_ok2,               "Device::SetDataFormat"},
        {dev_ok2,               "Device::SetEventNotification"},
        {dev_ok3,               "Device::SetCooperativeLevel"},
        {dev_ok4,               "Device::GetObjectInfo"},
        {dev_ok2,               "Device::GetDeviceInfo"},
        {dev_ok3,               "Device::RunControlPanel"},
        {dev_ok4,               "Device::Initialize"},
    };
    g_vtbl_di = build_vtable(di, sizeof(di) / sizeof(di[0]));
    g_vtbl_didev = build_vtable(dev, sizeof(dev) / sizeof(dev[0]));
}

/* ------------------------------------------------- DirectDrawCreate */

/* The entry point GetProcAddress hands out. */
static void ddraw_DirectDrawCreate(void) {
    uint32_t ppdd = ARG(1);
    uint32_t o = obj_new(g_vtbl_dd, KIND_DD);
    if (ppdd) MEM32(ppdd) = o;
    fprintf(stderr, "[dd] DirectDrawCreate -> 0x%08X\n", o);
    RET(o ? DD_OK : DDERR_UNSUPPORTED); STDRET(3);
}

/*
 * DirectDrawCreateEx is the DirectX 7 marker. The version probe finds the
 * DirectX 6 features, then looks for this export to decide whether 7 is
 * present -- so without it the answer comes back 0x600 and a game built
 * against 7 stops.
 *
 * It takes the IID as a third argument and returns that interface directly,
 * rather than requiring a QueryInterface afterwards.
 */
static void ddraw_DirectDrawCreateEx(void) {
    uint32_t ppdd = ARG(1);
    uint32_t o = obj_new(g_vtbl_dd, KIND_DD);
    if (ppdd) MEM32(ppdd) = o;
    fprintf(stderr, "[dd] DirectDrawCreateEx -> 0x%08X\n", o);
    RET(o ? DD_OK : DDERR_UNSUPPORTED); STDRET(4);
}

/*
 * Enumeration has to CALL BACK into lifted code, and a stub that returns
 * DD_OK without doing so leaves the game with an empty device list -- which it
 * then indexes, reading 0x910E06F8 out of uninitialised stack.
 *
 * The A and Ex forms are NOT interchangeable, and sharing one shim between them
 * was a purge bug: DirectDrawEnumerateA takes 2 arguments and its callback 4,
 * while DirectDrawEnumerateExA takes 3 and its callback 5 (the extra one is an
 * HMONITOR). One device is reported -- the primary display -- because that is
 * the one the software renderer draws to.
 */
static void enum_callback(uint32_t cb, uint32_t ctx, int ex) {
    recomp_func_t f = recomp_lookup(cb);
    if (!f) {
        /* A callback inside the image that the catalog missed: it will show up
         * as an unresolved dispatch rather than silently doing nothing. */
        fprintf(stderr, "[dd] enum callback 0x%08X is not in the dispatch "
                        "table\n", cb);
        return;
    }
    uint32_t desc = crt_alloc(64), name = crt_alloc(32);
    strcpy((char*)(uintptr_t)ADDR(desc), "Primary Display Driver");
    strcpy((char*)(uintptr_t)ADDR(name), "display");

    /* stdcall: push right to left, then the dummy return address. The
     * callback's own `ret` pops all of it. */
    if (ex) PUSH32(g_esp, 0);          /* hMonitor */
    PUSH32(g_esp, ctx);
    PUSH32(g_esp, name);
    PUSH32(g_esp, desc);
    PUSH32(g_esp, 0);                  /* lpGUID = NULL -> the primary device */
    PUSH32(g_esp, RECOMP_RETADDR);
    f();
}

static void ddraw_DirectDrawEnumerateA(void) {
    uint32_t cb = ARG(0), ctx = ARG(1);
    uint32_t save = g_esp;
    fprintf(stderr, "[dd] DirectDrawEnumerateA(cb=0x%08X)\n", cb);
    enum_callback(cb, ctx, 0);
    g_esp = save;
    RET(DD_OK); STDRET(2);
}

static void ddraw_DirectDrawEnumerateExA(void) {
    uint32_t cb = ARG(0), ctx = ARG(1);
    uint32_t save = g_esp;
    fprintf(stderr, "[dd] DirectDrawEnumerateExA(cb=0x%08X, flags=0x%X)\n",
            cb, ARG(2));
    enum_callback(cb, ctx, 1);
    g_esp = save;
    RET(DD_OK); STDRET(3);
}

/*
 * Resolve a name GetProcAddress was asked for. Returns a synthetic VA, so the
 * game can store it, call through it, and land in C.
 */
uint32_t ddraw_proc(const char* name) {
    static uint32_t va_create, va_enum;
    if (!strcmp(name, "DirectDrawCreate")) {
        if (!va_create) va_create = method(ddraw_DirectDrawCreate,
                                           "DirectDrawCreate");
        return va_create;
    }
    if (!strcmp(name, "DirectDrawEnumerateA")) {
        if (!va_enum) va_enum = method(ddraw_DirectDrawEnumerateA,
                                       "DirectDrawEnumerateA");
        return va_enum;
    }
    static uint32_t va_enumex;
    if (!strcmp(name, "DirectDrawEnumerateExA")) {
        if (!va_enumex) va_enumex = method(ddraw_DirectDrawEnumerateExA,
                                           "DirectDrawEnumerateExA");
        return va_enumex;
    }
    static uint32_t va_createex;
    if (!strcmp(name, "DirectDrawCreateEx")) {
        if (!va_createex) va_createex = method(ddraw_DirectDrawCreateEx,
                                               "DirectDrawCreateEx");
        return va_createex;
    }
    static uint32_t va_di;
    if (!strcmp(name, "DirectInputCreateA")) {
        if (!va_di) va_di = method(dinput_DirectInputCreateA,
                                   "DirectInputCreateA");
        return va_di;
    }
    return 0;
}

/* ------------------------------------------- a generic COM object

 * The DirectX version probe needs CoCreateInstance(CLSID_DirectMusic) to
 * succeed -- not to do anything, just to hand back something it can Release:
 *
 *     CoCreateInstance(CLSID_DirectMusic, ...)   fails  -> version 0x600
 *                                                succeeds -> 0x601, then
 *     GetProcAddress(ddraw, "DirectDrawCreateEx")        -> 0x700
 *
 * So a refusal caps the answer at DirectX 6 and the game exits. This is the
 * object that gets it past 0x601.
 *
 * Its vtable is IUnknown followed by slots that ABORT rather than return.
 * Returning a plausible success from an unknown COM method is how the
 * CoCreateInstance bug happened in the first place; aborting names the slot in
 * the [com] line printed just before it, which says exactly what to implement.
 */
#define GENERIC_SLOTS 64

static void gen_unimplemented(void) {
    fprintf(stderr,
        "\n[com] unimplemented method on a generic object (the [com] line above\n"
        "      names it). No purge count is known for it, so returning would\n"
        "      desynchronise the stack silently. Implement it in ddraw_shims.c.\n");
    abort();
}

static uint32_t g_vtbl_generic;

static void generic_init(void) {
    uint32_t v = crt_alloc(GENERIC_SLOTS * 4 + 4);
    MEM32(v + 0) = method(m_QueryInterface, "Generic::QueryInterface");
    MEM32(v + 4) = method(m_AddRef,         "Generic::AddRef");
    MEM32(v + 8) = method(m_Release,        "Generic::Release");
    static const char* names[GENERIC_SLOTS];
    for (unsigned i = 3; i < GENERIC_SLOTS; i++) {
        static char buf[GENERIC_SLOTS][24];
        snprintf(buf[i], sizeof(buf[i]), "Generic::slot%u", i);
        names[i] = buf[i];
        MEM32(v + i * 4) = method(gen_unimplemented, names[i]);
    }
    g_vtbl_generic = v;
}

/*
 * CoCreateInstance. Answers with a generic object so the probe can Release it.
 * Every CLSID is accepted deliberately: the alternative is a table of GUIDs
 * that has to be guessed at, and an unknown class that returns an object whose
 * methods abort is more informative than one that is refused.
 */
uint32_t ddraw_cocreate(uint32_t clsid_guid) {
    if (!g_vtbl_generic) generic_init();
    uint32_t o = obj_new(g_vtbl_generic, KIND_DD);
    fprintf(stderr, "[ole] CoCreateInstance({%08X-...}) -> 0x%08X (generic)\n",
            clsid_guid, o);
    return o;
}

/* Let other shim files hand out a callable synthetic VA (GetProcAddress for a
 * function that lives outside this file, such as USER32's GetMonitorInfoA). */
uint32_t ddraw_register_host_proc(import_fn_t fn, const char* name) {
    for (unsigned i = 0; i < g_method_n; i++)
        if (g_methods[i] == fn) return METHOD_BASE + i * 4;
    return method(fn, name);
}
