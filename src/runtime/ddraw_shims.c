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
/* For D3DDEVICEDESC7 and the device GUIDs. The struct carries no pointers, so
 * its 32- and 64-bit layouts are identical (236 bytes, checked against the
 * header with offsetof) and a host-built one copies straight into target
 * memory. Filling 60 fields by hand offset would be the alternative. */
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>

#include "recomp_types.h"
#include "imports.h"

uint32_t crt_alloc(uint32_t n);
void*    host_surface(void);
int      host_width(void);
int      host_height(void);
#include "raster.h"

void     host_present(void);
void     host_resize(int w, int h);

#define DD_OK             0
#define DDERR_UNSUPPORTED 0x80004001u
#define DDERR_NOTFOUND    0x887600FFu   /* MAKE_DDHRESULT(255) */
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
        /* g_cur_func is the lifted function that made the call: a shim is not
         * lifted and never overwrites it, so it still names the caller. That
         * is the one fact needed to go and read the decision being made. */
        fprintf(stderr, "[com] %-38s <- 0x%08X\n",
                g_method_names[i], g_cur_func);
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
 *   +0x28  the IDirectDraw this surface was created from
 *   +0x2C  an explicitly attached surface (the Z buffer)
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
#define O_OWNER(o)  MEM32((o) + 0x28)
#define O_ATTACH(o) MEM32((o) + 0x2C)
#define O_PF(o)     MEM32((o) + 0x30)   /* RPF_*, from the DDPIXELFORMAT */
#define O_SIZE      0x34

#define KIND_DD      1
#define KIND_SURFACE 2

#define DDSCAPS_PRIMARYSURFACE 0x00000200u
#define DDSCAPS_BACKBUFFER     0x00000004u
#define DDSCAPS_FLIP           0x00000010u
#define DDSCAPS_ZBUFFER        0x00020000u
#define DDSCAPS_MIPMAP         0x00400000u
#define DDSCAPS_TEXTURE        0x00001000u

static uint32_t g_vtbl_dd, g_vtbl_surf;
uint32_t ddraw_d3d_vtable(void);   /* defined near the Direct3D block */
static uint32_t g_primary;             /* the surface presented to the window */
static uint32_t g_d3d_rt;              /* the Direct3D render target */
uint32_t g_d3d_prims;                  /* primitives accepted */
uint32_t g_d3d_pixels;                 /* pixels rasterised */
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
/* --dumpframe PATH: write the first DUMP_PRESENTS frames the game presents to
 * PATH.N.bmp. See ddraw_dump_surface. */
#define DUMP_PRESENTS 5
static const char* g_dump_path;
void ddraw_set_dumpframe(const char* p) { g_dump_path = p; }
uint32_t ddraw_dump_surface(uint32_t s, const char* path);

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
    /* The first few presents say whether the frame has anything in it. A black
     * window and no window at all look the same from a log. */
    static unsigned np;
    /* The first frames are the clear colour before the UI has anything to
     * draw, so sample early AND every 50th. np counts every present, not just
     * the reported ones -- gating the increment on the report is how the log
     * stopped dead at five. */
    np++;
    if (np <= DUMP_PRESENTS || np % 50 == 0) {
        uint32_t nz = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (dst[(size_t)y * hw + x] & 0xFFFFFFu) nz++;
        fprintf(stderr, "[present] #%u surface 0x%08X %dx%d: %u of %d pixels"
                        " non-black, %u rasterised in %u prims\n",
                np, s, w, h, nz, w * h, g_d3d_pixels, g_d3d_prims);
        if (g_dump_path) {
            char path[512];
            snprintf(path, sizeof path, "%s.%u.bmp", g_dump_path, np);
            ddraw_dump_surface(s, path);
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

    if (is_d3d) {
        uint32_t vt = ddraw_d3d_vtable();
        uint32_t d = obj_new(vt, KIND_DD);
        if (ppv) MEM32(ppv) = d;
        fprintf(stderr, "[com] QueryInterface {%08X-...} -> IDirect3D7 "
                        "0x%08X\n", g, d);
        RET(d ? DD_OK : E_NOINTERFACE); STDRET(3);
        return;
    }
    if (!is_dd && !is_surf) {
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

/*
 * SetDisplayMode(w, h, bpp, refresh, flags) -- FIVE arguments.
 *
 * IDirectDraw v1 took three, and the shim purged for three. The game holds an
 * IDirectDraw7 (it came from DirectDrawCreateEx, and it calls GetAvailableVidMem
 * which only exists from DD2 on), so eight bytes of refresh rate and flags were
 * left on the target stack. The caller then popped its saved registers off the
 * wrong slots: CUtilityDevice::SelectRenderer came back from creating the
 * screen with `this` == 0 and faulted writing this+0x48. Two registers of
 * collateral from one wrong purge count, a long way from the shim.
 */
static void dd_SetDisplayMode(void) {
    g_mode_w = (int)ARG(1);
    g_mode_h = (int)ARG(2);
    g_mode_bpp = (int)ARG(3);
    fprintf(stderr, "[dd] SetDisplayMode %dx%d %dbpp refresh=%u flags=0x%X\n",
            g_mode_w, g_mode_h, g_mode_bpp, ARG(4), ARG(5));
    host_resize(g_mode_w, g_mode_h);
    RET(DD_OK); STDRET(6);
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
    MEM32(c + 0x04) = 0x00000001u /* DDCAPS_3D: worth 0x200 of the mask the
                                     acceptance check at 0x007318C0 builds */ |
                      DDCAPS_BLT | DDCAPS_BLTQUEUE | DDCAPS_BLTFOURCC |
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

    /* A bit depth is not a pixel format. DDPIXELFORMAT sits at desc+0x48 and
     * its masks are the only thing that distinguishes 565 from 1555 from 4444,
     * or XRGB8888 from ARGB8888. Decoding the font atlas as 565 when it is
     * 1555 is what put a solid red panel over the game's own title. */
    if (desc && MEM32(desc + 0x48)) {
        uint32_t rm = MEM32(desc + 0x58), am = MEM32(desc + 0x64);
        if (O_BPP(s) == 16)
            O_PF(s) = (am == 0x8000u) ? RPF_ARGB1555
                    : (am == 0xF000u) ? RPF_ARGB4444
                    : (rm == 0x7C00u) ? RPF_ARGB1555 : RPF_RGB565;
        else if (O_BPP(s) == 32)
            O_PF(s) = am ? RPF_ARGB8888 : RPF_XRGB8888;
    }

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        g_primary = s;
        /* A flipping primary gets a back buffer, and the game renders there. */
        if (backs || (caps & DDSCAPS_FLIP)) {
            uint32_t b = make_surface(w, h, O_BPP(s), DDSCAPS_BACKBUFFER);
            O_BACK(s) = b;
            if (b) O_OWNER(b) = ARG(0);
        }
    }
    O_OWNER(s) = ARG(0);
    if (pps) MEM32(pps) = s;
    fprintf(stderr, "[dd] CreateSurface %ux%u %ubpp caps=0x%X flags=0x%X -> "
                    "0x%08X%s\n", w, h, O_BPP(s), caps, flags, s,
            (caps & DDSCAPS_PRIMARYSURFACE) ? " (primary)" : "");
    /* DDPIXELFORMAT starts at desc+0x48: size, flags, fourcc, bitcount, then
     * the R/G/B/alpha masks. A 16-bit texture is 565, 1555 or 4444 and only
     * these say which. */
    if (desc && MEM32(desc + 0x48))
        fprintf(stderr, "[dd]   pf flags=0x%X bits=%u R=%08X G=%08X B=%08X"
                        " A=%08X\n",
                MEM32(desc + 0x4C), MEM32(desc + 0x54), MEM32(desc + 0x58),
                MEM32(desc + 0x5C), MEM32(desc + 0x60), MEM32(desc + 0x64));
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
 * direction across the boundary.
 *
 * The descriptor is a DDSURFACEDESC2, because the game reached this through
 * IDirectDraw7. In the 32-bit ABI that is 124 bytes -- NOT the 108 this used to
 * report, which is DDSURFACEDESC version 1. (The header's sizeof is 136 on a
 * 64-bit build: lpSurface is a pointer, so the 64-bit layout is 8 bytes longer
 * and every field after +0x24 moves. The offsets below are the 32-bit ones and
 * are what the original shim already used -- only the size and the flags were
 * from the wrong version.)
 *
 * A LIST of modes is offered rather than one. The game picks a mode from what
 * it is given and configures its renderer around it; offered a single mode that
 * happens not to match what it wants, it has nothing to choose.
 */
#define DDSD2_SIZE 0x7C

static void dd_mode_desc(uint32_t d, uint32_t w, uint32_t h, uint32_t bpp) {
    for (uint32_t i = 0; i < DDSD2_SIZE; i += 4) MEM32(d + i) = 0;
    MEM32(d + 0x00) = DDSD2_SIZE;
    /* CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT|REFRESHRATE */
    MEM32(d + 0x04) = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x40000;
    MEM32(d + 0x08) = h;
    MEM32(d + 0x0C) = w;
    MEM32(d + 0x10) = w * (bpp / 8);          /* lPitch */
    MEM32(d + 0x18) = 60;                     /* dwRefreshRate */
    MEM32(d + 0x48) = 32;                     /* ddpfPixelFormat.dwSize */
    MEM32(d + 0x4C) = 0x40;                   /* DDPF_RGB */
    MEM32(d + 0x54) = bpp;                    /* dwRGBBitCount */
    if (bpp == 16) {
        MEM32(d + 0x58) = 0xF800;             /* 5-6-5 */
        MEM32(d + 0x5C) = 0x07E0;
        MEM32(d + 0x60) = 0x001F;
    } else {
        MEM32(d + 0x58) = 0x00FF0000;         /* 8-8-8 */
        MEM32(d + 0x5C) = 0x0000FF00;
        MEM32(d + 0x60) = 0x000000FF;
    }
    MEM32(d + 0x68) = 0x00000200;             /* ddsCaps: DDSCAPS_PRIMARYSURFACE */
}

static void dd_EnumDisplayModes(void) {
    static const struct { uint32_t w, h; } modes[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024},
    };
    static const uint32_t depths[] = {16, 32};
    uint32_t ctx = ARG(3), cb = ARG(4);
    recomp_func_t f = cb ? recomp_lookup(cb) : NULL;
    if (!f) {
        if (cb) fprintf(stderr, "[dd] EnumDisplayModes: callback 0x%08X is not "
                                "in the dispatch table\n", cb);
        RET(DD_OK); STDRET(5); return;
    }
    uint32_t d = crt_alloc(DDSD2_SIZE);
    unsigned offered = 0;
    for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; m++) {
        for (unsigned b = 0; b < 2; b++) {
            dd_mode_desc(d, modes[m].w, modes[m].h, depths[b]);
            uint32_t save = g_esp, save_fn = g_cur_func;
            PUSH32(g_esp, ctx);
            PUSH32(g_esp, d);
            PUSH32(g_esp, RECOMP_RETADDR);
            f();                      /* the callback's `ret 8` balances it */
            g_cur_func = save_fn;
            if (g_esp != save) g_esp = save;
            offered++;
            if (g_eax == 0) {         /* DDENUMRET_CANCEL: it has seen enough */
                fprintf(stderr, "[dd] EnumDisplayModes -> %u modes, cancelled\n",
                        offered);
                RET(DD_OK); STDRET(5); return;
            }
        }
    }
    fprintf(stderr, "[dd] EnumDisplayModes -> %u modes offered\n", offered);
    RET(DD_OK); STDRET(5);
}
static void dd_EnumSurfaces(void) { RET(DD_OK); STDRET(5); }

/* ------------------------------------------------ IDirectDrawSurface */

static void sf_Lock(void) {
    uint32_t s = ARG(0), desc = ARG(2);
    if (desc) {
        for (uint32_t i = 0; i < DDSD2_SIZE; i += 4) MEM32(desc + i) = 0;
        MEM32(desc + 0x00) = DDSD2_SIZE;
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

/*
 * --dumpframe PATH: write the render target to a BMP, from whichever lifted
 * thread is running.
 *
 * The game sets a mode, creates a flipping primary with a back buffer and a
 * Z buffer, creates a Direct3D device against the back buffer, uploads
 * textures and sets render state -- and never calls Flip, because
 * CDD7FSScreen::Present (sub_007363B0) is never reached. That leaves one
 * question that reading code cannot answer: is there an image sitting in that
 * surface, or is nothing being drawn into it?
 *
 * A host thread that presented on a timer was tried first and never ticked:
 * present_surface ends in UpdateWindow, which waits on the window owner's
 * message pump, and stderr from a second thread under redirection is its own
 * hazard. A file needs neither. The non-black pixel count goes in the log, so
 * a run answers the question without anyone opening the image.
 *
 * This is a diagnostic, not a fix. The real present has to come from the game.
 */
uint32_t ddraw_dump_target(const char* path) {
    return ddraw_dump_surface(g_d3d_rt ? g_d3d_rt : g_primary, path);
}

uint32_t ddraw_dump_surface(uint32_t s, const char* path) {
    if (!s || !O_BITS(s)) {
        fprintf(stderr, "[dump] nothing to dump (rt=0x%08X primary=0x%08X)\n",
                g_d3d_rt, g_primary);
        return 0xFFFFFFFFu;
    }
    uint32_t w = O_W(s), h = O_H(s), bpp = O_BPP(s), pitch = O_PITCH(s);
    const uint8_t* base = (const uint8_t*)(uintptr_t)ADDR(O_BITS(s));
    uint32_t* out = (uint32_t*)malloc((size_t)w * h * 4);
    if (!out) return 0xFFFFFFFFu;

    uint32_t nz = 0;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t* d = out + (size_t)y * w;
        if (bpp == 16) {
            const uint16_t* r = (const uint16_t*)(base + (size_t)y * pitch);
            for (uint32_t x = 0; x < w; x++) {
                uint16_t v = r[x];
                d[x] = (uint32_t)(((v & 0xF800u) << 8) | ((v & 0x07E0u) << 5)
                                | ((v & 0x001Fu) << 3));
                if (v) nz++;
            }
        } else if (bpp == 32) {
            const uint32_t* r = (const uint32_t*)(base + (size_t)y * pitch);
            for (uint32_t x = 0; x < w; x++) {
                d[x] = r[x];
                if (r[x] & 0xFFFFFFu) nz++;
            }
        } else {
            memset(d, 0, (size_t)w * 4);
        }
    }

    uint32_t px = w * h * 4, fsz = 14 + 40 + px, off = 14 + 40;
    uint8_t fh[14] = {'B', 'M'};
    memcpy(fh + 2, &fsz, 4);
    memcpy(fh + 10, &off, 4);
    uint8_t ih[40] = {0};
    uint32_t v40 = 40, planes_bits = (1u) | (32u << 16), zero = 0;
    int32_t negh = -(int32_t)h;
    memcpy(ih + 0, &v40, 4);
    memcpy(ih + 4, &w, 4);
    memcpy(ih + 8, &negh, 4);          /* top-down */
    memcpy(ih + 12, &planes_bits, 4);
    memcpy(ih + 16, &zero, 4);         /* BI_RGB */
    memcpy(ih + 20, &px, 4);

    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(fh, 1, 14, f);
        fwrite(ih, 1, 40, f);
        fwrite(out, 1, px, f);
        fclose(f);
    }
    fprintf(stderr, "[dump] %s: surface 0x%08X %ux%u %ubpp,"
                    " %u of %u pixels non-black\n",
            f ? path : "(write failed)", s, w, h, bpp, nz, w * h);
    free(out);
    return nz;
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
        for (uint32_t i = 0; i < DDSD2_SIZE; i += 4) MEM32(d + i) = 0;
        MEM32(d + 0x00) = DDSD2_SIZE;
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

/*
 * DDDEVICEIDENTIFIER2 -- 1068 bytes: two 512-byte strings, an 8-byte driver
 * version, vendor/device/subsys/revision, a GUID and dwWHQLLevel.
 *
 * This used to be an "return DD_OK and write nothing" stub, and the game read
 * the uninitialised buffer back and logged it:
 *
 *     [dbg] Driver: ''   Description: ''   VendorId: 00000030
 *     [dbg] WHQLLevel: 117abb00
 *
 * A 1999 title reads this to decide which card-specific workarounds to switch
 * on, so leaving it as stack garbage is a coin flip on every one of them. An
 * unknown vendor with a WHQL-signed driver is the combination that matches no
 * blacklist entry.
 */
static void dd_GetDeviceIdentifier(void) {
    uint32_t out = ARG(1);
    if (!out) { RET(DDERR_INVALIDPARAMS); STDRET(3); return; }
    uint8_t* p = (uint8_t*)(uintptr_t)ADDR(out);
    memset(p, 0, 1068);
    strcpy((char*)p, "ddraw.dll");
    strcpy((char*)p + 512, "Direct3D HAL");
    MEM32(out + 1024) = 0;            /* liDriverVersion low  */
    MEM32(out + 1028) = 4 << 16;      /* high: 4.x            */
    MEM32(out + 1064) = 1;            /* dwWHQLLevel: signed  */
    RET(DD_OK); STDRET(3);
}

static void sf_GetCaps(void) {
    if (ARG(1)) MEM32(ARG(1)) = O_CAPS(ARG(0));
    RET(DD_OK); STDRET(2);
}

/*
 * GetDDInterface(LPVOID*) -- hand back the IDirectDraw that owns the surface.
 *
 * It was sf_ok2, which returns DD_OK and leaves the out pointer holding
 * whatever the caller had there. RE3D (sub_00773DF0) calls straight through
 * the result to get at the device, so a stub here is a fault one call later
 * with nothing to connect it to.
 */
static void sf_GetDDInterface(void) {
    uint32_t dd = O_OWNER(ARG(0));
    if (ARG(1)) MEM32(ARG(1)) = dd;
    if (dd) O_REF(dd)++;
    RET(dd ? DD_OK : DDERR_UNSUPPORTED); STDRET(2);
}

/*
 * GetAttachedSurface(LPDDSCAPS2, LPDIRECTDRAWSURFACE7*) -- and it has to be
 * able to say no.
 *
 * This returned the surface itself when it had nothing attached, on the theory
 * that a non-NULL answer was the safer one. It is the opposite: the DX7
 * texture manager walks a mipmap chain with
 *
 *     while (surf) { Lock(surf); record the format; surf = next mip level; }
 *
 * and the terminator is GetAttachedSurface refusing. Handing back the same
 * surface every time made that loop run forever -- 280,000 iterations, each
 * appending to two vectors, until the whole gigabyte of target heap was gone
 * and the failure surfaced as a null-pointer memset inside msvcrt.
 *
 * So: match the caps that were asked for, and DDERR_NOTFOUND when nothing
 * does. A flipping primary has a back buffer; a render target has whatever
 * AddAttachedSurface gave it (the Z buffer); nothing here has mipmaps.
 */
static void sf_GetAttachedSurface(void) {
    uint32_t s = ARG(0), pcaps = ARG(1), out = ARG(2);
    uint32_t want = pcaps ? MEM32(pcaps) : 0;      /* DDSCAPS2.dwCaps */
    uint32_t r = 0;

    if ((want & DDSCAPS_BACKBUFFER) && O_BACK(s)) r = O_BACK(s);
    else if (O_ATTACH(s) && (!want || (want & O_CAPS(O_ATTACH(s)))))
        r = O_ATTACH(s);
    else if (!want && O_BACK(s)) r = O_BACK(s);

    if (out) MEM32(out) = r;
    if (!r) { RET(DDERR_NOTFOUND); STDRET(3); return; }
    O_REF(r)++;
    RET(DD_OK); STDRET(3);
}

/* AddAttachedSurface(surf): one slot is enough -- the game attaches a Z buffer
 * to its render target and nothing else. */
static void sf_AddAttachedSurface(void) {
    O_ATTACH(ARG(0)) = ARG(1);
    if (ARG(1)) O_REF(ARG(1))++;
    RET(DD_OK); STDRET(2);
}

static void sf_DeleteAttachedSurface(void) {
    if (!ARG(2) || ARG(2) == O_ATTACH(ARG(0))) O_ATTACH(ARG(0)) = 0;
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
        {dd_GetDeviceIdentifier,  "IDirectDraw4::GetDeviceIdentifier"},
        {sf_ok4,                  "IDirectDraw7::StartModeTest"},
        {sf_ok3,                  "IDirectDraw7::EvaluateMode"},
    };
    static const vtent_t sf[] = {
        {m_QueryInterface,        "Surface::QueryInterface"},
        {m_AddRef,                "Surface::AddRef"},
        {m_Release,               "Surface::Release"},
        {sf_AddAttachedSurface,   "Surface::AddAttachedSurface"},
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
        {sf_GetDDInterface,       "Surface2::GetDDInterface"},
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

/*
 * No input: zero the buffer. A device that answers cleanly with nothing
 * pressed is what lets the game run its own loop.
 *
 * Synthetic input used to be fed in here, as DIMOUSESTATE deltas -- home the
 * cursor into a corner with a large negative delta, walk it out to a known
 * position, then set rgbButtons[0]. It fired, and the front end did not move a
 * pixel: the game does not take its cursor from this device. Input goes
 * through the window procedure, so --click posts real window messages now.
 * See host_input() in recomp_runtime.c.
 */
static void dev_GetDeviceState(void) {
    uint32_t n = ARG(1), p = ARG(2);
    if (p && n && n < 0x10000)
        memset((void*)(uintptr_t)ADDR(p), 0, n);
    RET(DI_OK); STDRET(3);
}

static void dev_GetDeviceData(void) {
    /* Buffered mode: report zero events by writing 0 back through pdwInOut. */
    { static unsigned c; if (++c % 500 == 1)
        fprintf(stderr, "[di] GetDeviceData #%u\n", c); }
    if (ARG(3)) MEM32(ARG(3)) = 0;
    RET(DI_OK); STDRET(5);
}

static void dev_ok1(void) { RET(DI_OK); STDRET(1); }
static void dev_ok2(void) { RET(DI_OK); STDRET(2); }
static void dev_ok3(void) { RET(DI_OK); STDRET(3); }
static void dev_ok4(void) { RET(DI_OK); STDRET(4); }
static void dev_ok5(void) { RET(DI_OK); STDRET(5); }

static void dinput_init(void);

/*
 * DirectInputCreateA(hinst, version, ppDI, punkOuter).
 *
 * dinput_init() had no caller at all: the vtables were built by a function
 * nobody ran, so this handed back an object whose vptr was 0 and
 * GamePPGlobalSysMouseManager faulted calling CreateDevice through it. The
 * version probe only ever asked GetProcAddress for the address and never
 * called it, so nothing had exercised the path.
 */
static void dinput_DirectInputCreateA(void) {
    if (!g_vtbl_di) dinput_init();
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
uint32_t ddraw_register_host_proc(import_fn_t fn, const char* name);

/* ------------------------------------------------------------ SMUSH.DLL
 *
 * The four entry points the game asks LoadLibrary/GetProcAddress for, and the
 * reason the boot script never finishes.
 *
 * Force Commander opens its first section by playing the intro movie: the boot
 * script starts a thread whose block is Loop Forever { Smush... Wait } and then
 * sits on a Wait If until that thread is done. With SMUSH.DLL's exports
 * unresolved the loader at 0x0068BE32 sees a NULL proc, FreeLibrary's the
 * module and zeroes its handle -- graceful, but the script still waits, and a
 * process whose every context is blocked is one the process manager retires.
 * The section then ends without a single frame ever being presented.
 *
 * Signatures come from the one caller, sub_0068D190, which is the movie player:
 *
 *     SmushSetVolume(vol)                         0x0068D283, esp += 4
 *     SmushStartup(surface)                       0x0068D296
 *     SmushPlay(name, 0xF, 0,0,0, 640, 480,       0x0068D2E8, esp += 0x34
 *               -1, 0, callback, 1, 1e6, 1e6)       -- 13 args, cdecl
 *     SmushShutdown()                             0x0068D30D
 *
 * SmushPlay is blocking -- the caller sets its state to 4 and shuts down on the
 * next line -- so returning at once is exactly "the movie finished", which is
 * what pressing a key during the intro does on real hardware.
 *
 * ponytail: no decoding. The movie is skipped, not played. Decoding SAN/NUT
 * belongs in its own file behind a --movies flag if the frames are ever wanted;
 * what the game needs from here is the completion, not the pixels.
 */
static void smush_Startup(void) {
    fprintf(stderr, "[smush] Startup(surface=0x%08X)\n", ARG(0));
    RET(1); CDECLRET();
}

static void smush_Play(void) {
    const char* n = ARG(0) ? (const char*)(uintptr_t)ADDR(ARG(0)) : "(null)";
    fprintf(stderr, "[smush] Play(\"%s\", %ux%u) -- skipped\n",
            n, ARG(5), ARG(6));
    RET(0); CDECLRET();
}

static void smush_Shutdown(void) {
    fprintf(stderr, "[smush] Shutdown\n");
    RET(0); CDECLRET();
}

static void smush_SetVolume(void) { RET(0); CDECLRET(); }

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
    static const struct { const char* name; import_fn_t fn; } smush[] = {
        {"SmushStartup",   smush_Startup},
        {"SmushPlay",      smush_Play},
        {"SmushShutdown",  smush_Shutdown},
        {"SmushSetVolume", smush_SetVolume},
    };
    for (unsigned i = 0; i < sizeof smush / sizeof smush[0]; i++)
        if (!strcmp(name, smush[i].name))
            return ddraw_register_host_proc(smush[i].fn, smush[i].name);
    return 0;
}

/*
 * The DirectX entry points the game imports STATICALLY.
 *
 * Everything else in this file is reached either through a vtable slot or
 * through the GetProcAddress hook above, so it took a fault to notice that
 * DirectInputCreateA is also in the import table -- the version probe at
 * 0x0076A6C0 asks GetProcAddress for it, but GamePPGlobalSysMouseManager
 * calls the import. That went to the generated stub, which returns without
 * filling the out pointer, so the manager kept a null IDirectInput and then
 * called CreateDevice through it.
 *
 * It is the only one: DDRAW, DPLAY and D3D all arrive through LoadLibrary or
 * CoCreateInstance. The table exists rather than a strcmp so the next one is
 * one line.
 */
import_fn_t ddraw_static_import(const char* qualified) {
    static const struct { const char* name; import_fn_t fn; } dx[] = {
        { "DINPUT.dll!DirectInputCreateA", dinput_DirectInputCreateA },
    };
    for (unsigned i = 0; i < sizeof dx / sizeof dx[0]; i++)
        if (!strcmp(dx[i].name, qualified)) return dx[i].fn;
    return NULL;
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

/*
 * DirectPlay. The game creates CLSID_DirectPlayLobby {2FE8F810-...} and
 * CLSID_DirectPlay {D1EB6D20-...} during startup even for a single-player run,
 * and calls slot 35 on one of them -- IDirectPlay3::EnumConnections, which asks
 * for the list of network service providers.
 *
 * An empty list is the right answer here. Focom.ini says `network disabled`,
 * and DirectPlay is a dead service anyway (see the README): whatever replaces
 * it is a design decision, not a recompilation one. The callback is simply not
 * invoked, and DP_OK reported.
 *
 * The purge count is what actually matters, and it is why the generic object
 * aborts rather than guessing: EnumConnections takes four arguments plus this.
 */
static void dp_EnumConnections(void)  { RET(0); STDRET(5); }   /* DP_OK, none */
static void dp_EnumSessions(void)     { RET(0); STDRET(6); }
static void dp_Close(void)            { RET(0); STDRET(1); }
static void dp_Initialize(void)       { RET(0); STDRET(2); }

/* IDirectPlayLobby3 is a DIFFERENT interface with a different slot order, so
 * it needs its own vtable -- sharing one had the lobby's RegisterApplication
 * land on IDirectPlay's GetGroupName. */
static void lob_RegisterApplication(void)   { RET(0); STDRET(3); }
static void lob_UnregisterApplication(void) { RET(0); STDRET(3); }
/*
 * GetConnectionSettings(appID, lpData, lpdwDataSize). The game was not
 * launched from a lobby, so the answer is DPERR_NOTLOBBIED --
 * MAKE_DPHRESULT(1070) = MAKE_HRESULT(1, _FACDP=0x877, 1070), which is
 * 0x8877042E. Taken from dplay.h rather than invented: a wrong DirectPlay
 * error is the kind of thing a game switches on.
 */
static void lob_GetConnectionSettings(void) { RET(0x8877042Eu); STDRET(4); }

static uint32_t g_vtbl_generic, g_vtbl_dplay, g_vtbl_lobby;

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

    /* IDirectPlay3/4: a copy of the aborting vtable with the slots the game
     * actually reaches filled in. */
    uint32_t d = crt_alloc(GENERIC_SLOTS * 4 + 4);
    memcpy((void*)(uintptr_t)ADDR(d), (void*)(uintptr_t)ADDR(v), GENERIC_SLOTS * 4);
    MEM32(d + 4  * 4) = method(dp_Close,           "IDirectPlay::Close");
    MEM32(d + 13 * 4) = method(dp_EnumSessions,    "IDirectPlay::EnumSessions");
    MEM32(d + 23 * 4) = method(dp_Initialize,      "IDirectPlay::Initialize");
    MEM32(d + 35 * 4) = method(dp_EnumConnections, "IDirectPlay::EnumConnections");
    g_vtbl_dplay = d;

    uint32_t l = crt_alloc(GENERIC_SLOTS * 4 + 4);
    memcpy((void*)(uintptr_t)ADDR(l), (void*)(uintptr_t)ADDR(v), GENERIC_SLOTS * 4);
    MEM32(l + 8  * 4) = method(lob_GetConnectionSettings,
                               "IDirectPlayLobby::GetConnectionSettings");
    MEM32(l + 16 * 4) = method(lob_RegisterApplication,
                               "IDirectPlayLobby::RegisterApplication");
    MEM32(l + 17 * 4) = method(lob_UnregisterApplication,
                               "IDirectPlayLobby::UnregisterApplication");
    g_vtbl_lobby = l;
}

/*
 * CoCreateInstance. Answers with a generic object so the probe can Release it.
 * Every CLSID is accepted deliberately: the alternative is a table of GUIDs
 * that has to be guessed at, and an unknown class that returns an object whose
 * methods abort is more informative than one that is refused.
 */
/* ----------------------------------------------------------- DirectSound
 *
 * The DirectX version probe at 0x0076A680 CoCreateInstances CLSID_DirectSound
 * and calls Initialize on it, and a generic object has no slot 10 -- which is
 * how this interface announced itself.
 *
 * ponytail: no audio. The game's actual sound path is Miles (mss32.dll), and
 * these are the answers a working DirectSound with no output device would
 * give: the object initialises, reports caps, hands out buffers that can be
 * locked and written to and report themselves stopped. Nothing is played.
 * Making sound means either loading the real mss32 on a 32-bit host or
 * decoding the formats, and neither is on the way to a first frame.
 */
#define DS_OK             0
#define DSERR_UNSUPPORTED 0x80004001u

static uint32_t g_vtbl_ds, g_vtbl_dsbuf;
static void dsound_init(void);

/* --- IDirectSoundBuffer. Offsets are from dsound.h, and the purge counts are
 *     the argument counts of those declarations. */
#define DSB_BYTES(o)  MEM32((o) + 0x0C)      /* reuse the surface width slot */
#define DSB_DATA(o)   MEM32((o) + 0x1C)      /* ...and the pixel-buffer slot */

static void dsb_GetCaps(void) {
    /* DSBCAPS: dwSize, dwFlags, dwBufferBytes, dwUnlockTransferRate,
     * dwPlayCpuOverhead. */
    uint32_t c = ARG(1);
    if (c) {
        MEM32(c + 0x00) = 20;
        MEM32(c + 0x04) = 0;
        MEM32(c + 0x08) = DSB_BYTES(ARG(0));
        MEM32(c + 0x0C) = 0;
        MEM32(c + 0x10) = 0;
    }
    RET(DS_OK); STDRET(2);
}
static void dsb_GetCurrentPosition(void) {
    if (ARG(1)) MEM32(ARG(1)) = 0;
    if (ARG(2)) MEM32(ARG(2)) = 0;
    RET(DS_OK); STDRET(3);
}
static void dsb_GetFormat(void) {
    /* WAVEFORMATEX for 16-bit stereo 22.05 kHz, which is what the game's own
     * .wav assets are. A getter that leaves the struct alone gives the caller
     * a sample rate off the stack. */
    uint32_t w = ARG(1), cb = ARG(2);
    if (w) {
        MEM16(w + 0x00) = 1;               /* WAVE_FORMAT_PCM */
        MEM16(w + 0x02) = 2;               /* channels */
        MEM32(w + 0x04) = 22050;           /* samples/sec */
        MEM32(w + 0x08) = 22050 * 4;       /* bytes/sec */
        MEM16(w + 0x0C) = 4;               /* block align */
        MEM16(w + 0x0E) = 16;              /* bits/sample */
        MEM16(w + 0x10) = 0;               /* cbSize */
    }
    if (cb) MEM32(cb) = 18;
    RET(DS_OK); STDRET(4);
}
static void dsb_GetVolume(void)   { if (ARG(1)) MEM32(ARG(1)) = 0; RET(DS_OK); STDRET(2); }
static void dsb_GetPan(void)      { if (ARG(1)) MEM32(ARG(1)) = 0; RET(DS_OK); STDRET(2); }
static void dsb_GetFrequency(void){ if (ARG(1)) MEM32(ARG(1)) = 22050; RET(DS_OK); STDRET(2); }
static void dsb_GetStatus(void)   { if (ARG(1)) MEM32(ARG(1)) = 0; RET(DS_OK); STDRET(2); }
static void dsb_Initialize(void)  { RET(DS_OK); STDRET(3); }

/*
 * Lock(offset, bytes, &ptr1, &len1, &ptr2, &len2, flags) -- the caller writes
 * samples through the pointers it gets back, so they have to be real target
 * memory. DSBLOCK_ENTIREBUFFER is 2.
 */
static void dsb_Lock(void) {
    uint32_t o = ARG(0), off = ARG(1), bytes = ARG(2);
    uint32_t total = DSB_BYTES(o);
    if ((ARG(7) & 2u) || bytes > total) bytes = total;
    if (off > total) off = 0;
    if (off + bytes > total) bytes = total - off;
    if (ARG(3)) MEM32(ARG(3)) = DSB_DATA(o) + off;
    if (ARG(4)) MEM32(ARG(4)) = bytes;
    if (ARG(5)) MEM32(ARG(5)) = 0;          /* no wrap-around second part */
    if (ARG(6)) MEM32(ARG(6)) = 0;
    RET(DSB_DATA(o) ? DS_OK : DSERR_UNSUPPORTED); STDRET(8);
}
static void dsb_Unlock(void)  { RET(DS_OK); STDRET(5); }
static void dsb_Play(void)    { RET(DS_OK); STDRET(4); }
static void dsb_Stop(void)    { RET(DS_OK); STDRET(1); }
static void dsb_set1(void)    { RET(DS_OK); STDRET(2); }
static void dsb_Restore(void) { RET(DS_OK); STDRET(1); }

/* --- IDirectSound */

static void ds_CreateSoundBuffer(void) {
    /* DSBUFFERDESC: dwSize, dwFlags, dwBufferBytes, dwReserved, lpwfxFormat */
    uint32_t desc = ARG(1);
    uint32_t bytes = desc ? MEM32(desc + 0x08) : 0;
    uint32_t o = obj_new(g_vtbl_dsbuf, KIND_DD);
    if (!o) { if (ARG(2)) MEM32(ARG(2)) = 0; RET(DSERR_UNSUPPORTED); STDRET(4); return; }
    /* A primary buffer is declared with dwBufferBytes 0; give it something
     * lockable anyway, because the game sets a format on it and may write. */
    if (!bytes) bytes = 4 * 22050;
    DSB_BYTES(o) = bytes;
    DSB_DATA(o) = crt_alloc(bytes);
    if (DSB_DATA(o)) memset((void*)(uintptr_t)ADDR(DSB_DATA(o)), 0, bytes);
    if (ARG(2)) MEM32(ARG(2)) = o;
    RET(DS_OK); STDRET(4);
}
static void ds_GetCaps(void) {
    /* DSCAPS, the fields anything reads: dwSize, dwFlags, dwMinSecondarySampleRate,
     * dwMaxSecondarySampleRate, dwPrimaryBuffers, then the hardware counts. */
    uint32_t c = ARG(1);
    if (c) {
        for (uint32_t i = 0; i < 0x60; i += 4) MEM32(c + i) = 0;
        MEM32(c + 0x00) = 0x60;
        MEM32(c + 0x04) = 0x00000200u | 0x00000020u;  /* SECONDARY16BIT|STEREO */
        MEM32(c + 0x08) = 100;
        MEM32(c + 0x0C) = 100000;
        MEM32(c + 0x10) = 1;
    }
    RET(DS_OK); STDRET(2);
}
static void ds_DuplicateSoundBuffer(void) {
    if (ARG(2)) MEM32(ARG(2)) = ARG(1);
    if (ARG(1)) O_REF(ARG(1))++;
    RET(DS_OK); STDRET(3);
}
static void ds_SetCooperativeLevel(void) { RET(DS_OK); STDRET(3); }
static void ds_Compact(void)             { RET(DS_OK); STDRET(1); }
static void ds_GetSpeakerConfig(void) {
    if (ARG(1)) MEM32(ARG(1)) = 1;         /* DSSPEAKER_HEADPHONE-ish default */
    RET(DS_OK); STDRET(2);
}
static void ds_SetSpeakerConfig(void)    { RET(DS_OK); STDRET(2); }
static void ds_Initialize(void)          { RET(DS_OK); STDRET(2); }

static void dsound_init(void) {
    static const vtent_t ds[] = {
        {m_QueryInterface,         "IDirectSound::QueryInterface"},
        {m_AddRef,                 "IDirectSound::AddRef"},
        {m_Release,                "IDirectSound::Release"},
        {ds_CreateSoundBuffer,     "IDirectSound::CreateSoundBuffer"},
        {ds_GetCaps,               "IDirectSound::GetCaps"},
        {ds_DuplicateSoundBuffer,  "IDirectSound::DuplicateSoundBuffer"},
        {ds_SetCooperativeLevel,   "IDirectSound::SetCooperativeLevel"},
        {ds_Compact,               "IDirectSound::Compact"},
        {ds_GetSpeakerConfig,      "IDirectSound::GetSpeakerConfig"},
        {ds_SetSpeakerConfig,      "IDirectSound::SetSpeakerConfig"},
        {ds_Initialize,            "IDirectSound::Initialize"},
    };
    static const vtent_t buf[] = {
        {m_QueryInterface,         "IDirectSoundBuffer::QueryInterface"},
        {m_AddRef,                 "IDirectSoundBuffer::AddRef"},
        {m_Release,                "IDirectSoundBuffer::Release"},
        {dsb_GetCaps,              "IDirectSoundBuffer::GetCaps"},
        {dsb_GetCurrentPosition,   "IDirectSoundBuffer::GetCurrentPosition"},
        {dsb_GetFormat,            "IDirectSoundBuffer::GetFormat"},
        {dsb_GetVolume,            "IDirectSoundBuffer::GetVolume"},
        {dsb_GetPan,               "IDirectSoundBuffer::GetPan"},
        {dsb_GetFrequency,         "IDirectSoundBuffer::GetFrequency"},
        {dsb_GetStatus,            "IDirectSoundBuffer::GetStatus"},
        {dsb_Initialize,           "IDirectSoundBuffer::Initialize"},
        {dsb_Lock,                 "IDirectSoundBuffer::Lock"},
        {dsb_Play,                 "IDirectSoundBuffer::Play"},
        {dsb_set1,                 "IDirectSoundBuffer::SetCurrentPosition"},
        {dsb_set1,                 "IDirectSoundBuffer::SetFormat"},
        {dsb_set1,                 "IDirectSoundBuffer::SetVolume"},
        {dsb_set1,                 "IDirectSoundBuffer::SetPan"},
        {dsb_set1,                 "IDirectSoundBuffer::SetFrequency"},
        {dsb_Stop,                 "IDirectSoundBuffer::Stop"},
        {dsb_Unlock,               "IDirectSoundBuffer::Unlock"},
        {dsb_Restore,              "IDirectSoundBuffer::Restore"},
    };
    g_vtbl_ds = build_vtable(ds, sizeof ds / sizeof ds[0]);
    g_vtbl_dsbuf = build_vtable(buf, sizeof buf / sizeof buf[0]);
}

uint32_t ddraw_cocreate(uint32_t clsid_guid) {
    if (!g_vtbl_generic) generic_init();
    /* The first dword is enough to tell the two DirectPlay classes apart. */
    uint32_t vt = g_vtbl_generic;
    const char* what = "generic";
    if (clsid_guid == 0xD1EB6D20u)      { vt = g_vtbl_dplay; what = "IDirectPlay"; }
    else if (clsid_guid == 0x2FE8F810u) { vt = g_vtbl_lobby; what = "IDirectPlayLobby"; }
    else if (clsid_guid == 0x47D4D946u) {          /* CLSID_DirectSound */
        if (!g_vtbl_ds) dsound_init();
        vt = g_vtbl_ds; what = "IDirectSound";
    }
    uint32_t o = obj_new(vt, KIND_DD);
    fprintf(stderr, "[ole] CoCreateInstance({%08X-...}) -> 0x%08X (%s)\n",
            clsid_guid, o, what);
    return o;
}

/* Let other shim files hand out a callable synthetic VA (GetProcAddress for a
 * function that lives outside this file, such as USER32's GetMonitorInfoA). */
uint32_t ddraw_register_host_proc(import_fn_t fn, const char* name) {
    for (unsigned i = 0; i < g_method_n; i++)
        if (g_methods[i] == fn) return METHOD_BASE + i * 4;
    return method(fn, name);
}

/* ------------------------------------------------------------ Direct3D 7

 * The device-acceptance check at 0x007318C0 builds a capability mask and
 * DDCAPS_3D is worth 0x200 of it. Without 3D the mask comes back 0x103 and the
 * device is rejected, so the software-only route does not exist as cleanly as
 * RE3D's CDD7MemRenderer suggested -- the game wants a 3D device.
 *
 * IDirect3D7 is small (8 methods). IDirect3DDevice7 is not, so rather than
 * guess at 40 methods its vtable names each slot and aborts: the [com] line
 * printed before the abort says precisely which method the game reached, which
 * turns "implement Direct3D" into an ordered list taken from a real run.
 */
static uint32_t g_vtbl_d3d, g_vtbl_d3ddev;
static void d3d_EnumDevices(void);
static void d3d_CreateDevice(void);
static void d3d_CreateVertexBuffer(void);
static void d3d_EnumZBufferFormats(void);
static void d3d_EvictManagedTextures(void);

/* IDirect3DDevice7 state, declared here because the IDirect3D7 methods that
 * create the device set the first two (g_d3d_rt is declared with g_primary,
 * which ddraw_present_target needs before this point). */
static uint32_t g_d3d_dev;                      /* the one device object */
static uint32_t g_d3d_devdesc;                  /* a target copy of the HAL desc */


/*
 * IDirect3D7::EnumDevices(callback, context).
 *
 * This used to offer nothing, on the reasoning that the caller's own "no
 * hardware device" path beat a fabricated description. The trace disagreed:
 * with no device the game has nothing to render with, and RE3D's driver object
 * comes back NULL -- sub_006BF7B0 then read [esi+0x30] off a null `this`. It is
 * the same shape as the DirectDraw enumeration (blocker #10 in
 * docs/STL-GATE.md): reporting success without calling back leaves an empty
 * list, and an empty list is not the same answer as "no hardware".
 *
 * One device is offered, the RGB software rasteriser, because that is the one
 * whose semantics this shim layer can actually honour -- RECON.md's finding
 * that RE3D has a CDD7MemRenderer is what makes a software device the honest
 * choice. Caps are set to what a complete DX7 software rasteriser reports.
 *
 * ponytail: one device, generous caps. Offer the HAL device too if the game
 * turns out to insist on one, and expect it to ask for things this layer does
 * not implement when it does.
 */
static void d3d_EnumDevices(void) {
    uint32_t cb = ARG(1), ctx = ARG(2);
    recomp_func_t f = recomp_lookup(cb);
    if (!f) {
        fprintf(stderr, "[d3d] EnumDevices: callback 0x%08X is not in the "
                        "dispatch table\n", cb);
        RET(DD_OK); STDRET(3); return;
    }

    D3DDEVICEDESC7 d;
    memset(&d, 0, sizeof d);
    d.dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX | D3DDEVCAPS_EXECUTESYSTEMMEMORY
                | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TEXTURESYSTEMMEMORY
                | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_DRAWPRIMITIVES2
                | D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWRASTERIZATION;
    d.dwDeviceRenderBitDepth  = DDBD_16 | DDBD_24 | DDBD_32;
    d.dwDeviceZBufferBitDepth = DDBD_16 | DDBD_32;
    d.dwMinTextureWidth = d.dwMinTextureHeight = 1;
    d.dwMaxTextureWidth = d.dwMaxTextureHeight = 2048;
    d.dwMaxTextureRepeat = 2048;
    d.dwMaxTextureAspectRatio = 2048;
    d.dwMaxAnisotropy = 1;
    d.dvGuardBandLeft = -32768.0f;  d.dvGuardBandTop    = -32768.0f;
    d.dvGuardBandRight = 32768.0f;  d.dvGuardBandBottom =  32768.0f;
    d.dwFVFCaps = 8;                       /* texture coordinate sets */
    d.dwTextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1
                      | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE
                      | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_ADD
                      | D3DTEXOPCAPS_BLENDDIFFUSEALPHA
                      | D3DTEXOPCAPS_BLENDTEXTUREALPHA;
    d.wMaxTextureBlendStages = 8;
    d.wMaxSimultaneousTextures = 1;        /* the software rasteriser's answer */
    d.dwMaxActiveLights = 8;
    d.dvMaxVertexW = 1.0e10f;

    /* Both D3DPRIMCAPS describe the same rasteriser. */
    D3DPRIMCAPS pc;
    memset(&pc, 0, sizeof pc);
    pc.dwSize = sizeof pc;
    pc.dwMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW
                  | D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_MASKZ;
    pc.dwRasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST
                    | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE
                    | D3DPRASTERCAPS_SUBPIXEL | D3DPRASTERCAPS_ZBIAS;
    pc.dwZCmpCaps = D3DPCMPCAPS_NEVER | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL
                  | D3DPCMPCAPS_LESSEQUAL | D3DPCMPCAPS_GREATER
                  | D3DPCMPCAPS_NOTEQUAL | D3DPCMPCAPS_GREATEREQUAL
                  | D3DPCMPCAPS_ALWAYS;
    pc.dwSrcBlendCaps = D3DPBLENDCAPS_ZERO | D3DPBLENDCAPS_ONE
                      | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA
                      | D3DPBLENDCAPS_SRCCOLOR | D3DPBLENDCAPS_INVSRCCOLOR
                      | D3DPBLENDCAPS_DESTCOLOR | D3DPBLENDCAPS_INVDESTCOLOR
                      | D3DPBLENDCAPS_DESTALPHA | D3DPBLENDCAPS_INVDESTALPHA;
    pc.dwDestBlendCaps = pc.dwSrcBlendCaps;
    pc.dwAlphaCmpCaps = pc.dwZCmpCaps;
    pc.dwShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB
                   | D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
    pc.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA
                     | D3DPTEXTURECAPS_TRANSPARENCY | D3DPTEXTURECAPS_POW2
                     | D3DPTEXTURECAPS_ALPHAPALETTE;
    pc.dwTextureFilterCaps = D3DPTFILTERCAPS_NEAREST | D3DPTFILTERCAPS_LINEAR
                           | D3DPTFILTERCAPS_MIPNEAREST | D3DPTFILTERCAPS_MIPLINEAR
                           | D3DPTFILTERCAPS_LINEARMIPNEAREST
                           | D3DPTFILTERCAPS_LINEARMIPLINEAR
                           | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR
                           | D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR;
    pc.dwTextureBlendCaps = D3DPTBLENDCAPS_DECAL | D3DPTBLENDCAPS_MODULATE
                          | D3DPTBLENDCAPS_DECALALPHA | D3DPTBLENDCAPS_MODULATEALPHA
                          | D3DPTBLENDCAPS_COPY | D3DPTBLENDCAPS_ADD;
    pc.dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR
                            | D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_INDEPENDENTUV;
    d.dpcLineCaps = pc;
    d.dpcTriCaps  = pc;

    /*
     * Two devices, and the HAL is not optional.
     *
     * The game's callback (sub_0076ACC0) files each device into one of four
     * fixed slots by GUID -- TnLHal, HAL, RGB, Ref -- and CDD7Device keeps all
     * four 236-byte descriptions inline, at device+0x2D4, +0x3E0, +0x4EC and
     * +0x5F8. Then sub_007321C0 -- the gate that decides whether the device can
     * have a screen at all -- reads dwDeviceRenderBitDepth at a HARDCODED
     * offset: device + 0x454, which is desc+0x74 of the HAL slot.
     *
     * Offering only the RGB software device left that slot zeroed, the
     * `test ah,4` for DDBD_16 failed, and the screen and renderer registration
     * in the rest of the constructor was skipped: no CDD7WinScreen, no
     * Direct3D device, and a front end that ran 61 frames with nothing to draw
     * on. The RGB description was sitting correctly in slot 3 at device+0x560
     * the whole time (0x700 = DDBD_16|24|32), which is how the offsets were
     * confirmed rather than guessed.
     *
     * So the HAL is announced as well, with the video-memory capability bits a
     * real accelerated part reports. Nothing behind it is hardware -- this
     * layer draws everything itself -- but "this machine has an accelerated
     * device" is true, and it is the question RE3D is actually asking.
     *
     * ponytail: TnLHal and Ref are still absent. Add them if something reads
     * their slots (device+0x348 and +0x66C are the same field); each one is
     * another set of caps to keep honest.
     */
    static const struct { GUID guid; const char* name; const char* desc;
                          uint32_t extra_devcaps; } devices[] = {
        {{0x84E63DE0,0x46AA,0x11CF,{0x81,0x6F,0x00,0x00,0xC0,0x20,0x15,0x6E}},
         "Direct3D HAL",
         "Microsoft Direct3D Hardware acceleration through Direct3D HAL",
         D3DDEVCAPS_EXECUTEVIDEOMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY
         | D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_TEXTURENONLOCALVIDMEM},
        {{0xA4665C60,0x2673,0x11CF,{0xA3,0x1A,0x00,0xAA,0x00,0xB9,0x33,0x56}},
         "RGB Emulation", "Microsoft Direct3D RGB Software Emulation", 0},
    };

    uint32_t base_devcaps = d.dwDevCaps;
    for (unsigned i = 0; i < sizeof devices / sizeof devices[0]; i++) {
        d.deviceGUID = devices[i].guid;
        d.dwDevCaps = base_devcaps | devices[i].extra_devcaps;

        uint32_t desc = crt_alloc(96), name = crt_alloc(32);
        uint32_t dd   = crt_alloc(sizeof d);
        strcpy((char*)(uintptr_t)ADDR(desc), devices[i].desc);
        strcpy((char*)(uintptr_t)ADDR(name), devices[i].name);
        memcpy((void*)(uintptr_t)ADDR(dd), &d, sizeof d);
        if (i == 0) g_d3d_devdesc = dd;         /* the HAL, for GetCaps */

        fprintf(stderr, "[d3d] EnumDevices callback=0x%08X -> %s"
                        " (desc %u bytes at 0x%08X)\n",
                cb, devices[i].name, (unsigned)sizeof d, dd);

        /* stdcall, pushed right to left, then the dummy return address the
         * callback's own `ret` will pop. D3DENUMRET_OK means "keep going". */
        uint32_t save = g_esp, save_fn = g_cur_func;
        PUSH32(g_esp, ctx);
        PUSH32(g_esp, dd);
        PUSH32(g_esp, name);
        PUSH32(g_esp, desc);
        PUSH32(g_esp, RECOMP_RETADDR);
        f();
        g_cur_func = save_fn;
        if (g_esp != save) {
            fprintf(stderr, "[d3d] EnumDevices callback left esp at 0x%08X,"
                            " expected 0x%08X\n", g_esp, save);
            g_esp = save;
        }
        if (g_eax == 0) break;      /* D3DENUMRET_CANCEL: it has seen enough */
    }
    RET(DD_OK); STDRET(3);
}

static void d3d_CreateDevice(void) {
    uint32_t o = obj_new(g_vtbl_d3ddev, KIND_DD);
    if (ARG(3)) MEM32(ARG(3)) = o;
    g_d3d_dev = o;
    /* CreateDevice(guid, surface, ppDevice): the surface it is created against
     * IS the initial render target, and RE3D never sets one explicitly before
     * its first Clear. */
    g_d3d_rt = ARG(2);
    fprintf(stderr, "[d3d] CreateDevice -> 0x%08X, render target 0x%08X\n",
            o, g_d3d_rt);
    RET(DD_OK); STDRET(4);
}

/* ------------------------------------------------- IDirect3DVertexBuffer7
 *
 * Nine methods, and the game reaches Lock on the first frame it renders.
 * It used to get a generic object whose slots abort, and slot 3 is Lock.
 *
 * D3DVERTEXBUFFERDESC is {dwSize, dwCaps, dwFVF, dwNumVertices}. The stride
 * that FVF implies is deliberately not computed: the buffer is only ever
 * written by the game and read back by DrawPrimitiveVB, so 64 bytes a vertex
 * is larger than any FVF DX7 can express and nothing needs the exact figure.
 *
 * ponytail: no vertex processing. ProcessVertices and ProcessVerticesStrided
 * accept and succeed without transforming anything -- they are the hardware
 * T&L path, and nothing is rasterised yet anyway. Compute the stride and do
 * the transform when there is a rasteriser to feed.
 */
#define VB_SLOTS  9
#define VB_STRIDE 64u
#define VB_NVERT(o) MEM32((o) + 0x10)        /* reuse the surface height slot */
#define VB_FVF(o)   MEM32((o) + 0x14)        /* ...the bpp slot */
#define VB_BYTES(o) MEM32((o) + 0x0C)        /* ...the width slot */
#define VB_DATA(o)  MEM32((o) + 0x1C)        /* ...the pixel-buffer slot */

static uint32_t g_vtbl_vbuf;

static void vb_Lock(void) {
    uint32_t o = ARG(0), out = ARG(2), sz = ARG(3);
    if (out) MEM32(out) = VB_DATA(o);
    if (sz) MEM32(sz) = VB_BYTES(o);
    RET(VB_DATA(o) ? DD_OK : DDERR_INVALIDPARAMS); STDRET(4);
}

static void vb_GetVertexBufferDesc(void) {
    uint32_t o = ARG(0), d = ARG(1);
    if (!d) { RET(DDERR_INVALIDPARAMS); STDRET(2); return; }
    MEM32(d + 0) = 16;
    MEM32(d + 4) = 0;
    MEM32(d + 8) = VB_FVF(o);
    MEM32(d + 12) = VB_NVERT(o);
    RET(DD_OK); STDRET(2);
}

static void vb_ProcessVertices(void) { RET(DD_OK); STDRET(8); }

static void vbuf_init(void) {
    if (!g_vtbl_generic) generic_init();
    uint32_t v = crt_alloc(VB_SLOTS * 4);
    static const vtent_t vb[VB_SLOTS] = {
        {m_QueryInterface,        "VertexBuffer::QueryInterface"},
        {m_AddRef,                "VertexBuffer::AddRef"},
        {m_Release,               "VertexBuffer::Release"},
        {vb_Lock,                 "VertexBuffer::Lock"},
        {sf_ok1,                  "VertexBuffer::Unlock"},
        {vb_ProcessVertices,      "VertexBuffer::ProcessVertices"},
        {vb_GetVertexBufferDesc,  "VertexBuffer::GetVertexBufferDesc"},
        {sf_ok3,                  "VertexBuffer::Optimize"},
        {vb_ProcessVertices,      "VertexBuffer::ProcessVerticesStrided"},
    };
    for (unsigned i = 0; i < VB_SLOTS; i++)
        MEM32(v + i * 4) = method(vb[i].fn, vb[i].nm);
    g_vtbl_vbuf = v;
}

static void d3d_CreateVertexBuffer(void) {
    uint32_t d = ARG(1), out = ARG(2);
    if (!g_vtbl_vbuf) vbuf_init();
    uint32_t o = obj_new(g_vtbl_vbuf, KIND_DD);
    uint32_t n = d ? MEM32(d + 12) : 0, fvf = d ? MEM32(d + 8) : 0;
    if (!n) n = 1;
    VB_NVERT(o) = n;
    VB_FVF(o) = fvf;
    VB_BYTES(o) = n * VB_STRIDE;
    VB_DATA(o) = crt_alloc(n * VB_STRIDE);
    if (VB_DATA(o)) memset((void*)(uintptr_t)ADDR(VB_DATA(o)), 0, n * VB_STRIDE);
    fprintf(stderr, "[d3d] CreateVertexBuffer %u vertices fvf=0x%X -> 0x%08X\n",
            n, fvf, o);
    if (out) MEM32(out) = o;
    RET(DD_OK); STDRET(4);
}

/*
 * EnumZBufferFormats(riidDevice, callback, context).
 *
 * Same shape as every other enumeration in this file, and the same trap:
 * returning DD_OK without calling back is an empty list, not "any format will
 * do". The game got a Z buffer anyway because it falls back to the display
 * depth, but a renderer that asks for the list and indexes it is one fault
 * away. 16-bit Z and 24-bit Z with 8 bits of stencil, which is what a DX7 part
 * of this era reported.
 */
static void d3d_EnumZBufferFormats(void) {
    uint32_t cb = ARG(2), ctx = ARG(3);
    recomp_func_t f = cb ? recomp_lookup(cb) : NULL;
    if (!f) { RET(DD_OK); STDRET(4); return; }

    static const struct { uint32_t zbits, sbits, zmask, smask; } zf[] = {
        {16, 0, 0x0000FFFFu, 0},
        {24, 8, 0xFFFFFF00u, 0x000000FFu},
        {32, 0, 0xFFFFFFFFu, 0},
    };
    uint32_t pf = crt_alloc(32);
    for (unsigned i = 0; i < sizeof zf / sizeof zf[0]; i++) {
        for (uint32_t k = 0; k < 32; k += 4) MEM32(pf + k) = 0;
        MEM32(pf + 0x00) = 32;                     /* dwSize */
        MEM32(pf + 0x04) = 0x00000400u             /* DDPF_ZBUFFER */
                         | (zf[i].sbits ? 0x00004000u : 0u); /* DDPF_STENCILBUFFER */
        MEM32(pf + 0x0C) = zf[i].zbits;            /* dwZBufferBitDepth */
        MEM32(pf + 0x10) = zf[i].sbits;            /* dwStencilBitDepth */
        MEM32(pf + 0x14) = zf[i].zmask;            /* dwZBitMask */
        MEM32(pf + 0x18) = zf[i].smask;            /* dwStencilBitMask */

        uint32_t save = g_esp, save_fn = g_cur_func;
        PUSH32(g_esp, ctx);
        PUSH32(g_esp, pf);
        PUSH32(g_esp, RECOMP_RETADDR);
        f();
        g_cur_func = save_fn;
        if (g_esp != save) g_esp = save;
        if (g_eax == 0) break;                     /* D3DENUMRET_CANCEL */
    }
    fprintf(stderr, "[d3d] EnumZBufferFormats -> %u formats offered\n",
            (unsigned)(sizeof zf / sizeof zf[0]));
    RET(DD_OK); STDRET(4);
}
static void d3d_EvictManagedTextures(void) { RET(DD_OK); STDRET(1); }

/* ------------------------------------------------- IDirect3DDevice7
 *
 * 49 methods, and the purge count of every one of them is a fact about the DX7
 * headers rather than a guess -- which matters, because getting SetDisplayMode
 * wrong by two arguments was enough to hand CUtilityDevice::SelectRenderer a
 * null `this` several calls later. So the table below carries the argument
 * count next to each name.
 *
 * The state setters remember what they were given and the getters hand it
 * back. That is not politeness: RE3D reads back the viewport and the render
 * states it set, and a getter that leaves the caller's struct alone gives it
 * whatever was on the stack.
 *
 * ponytail: no rasterisation. Clear() really does clear the render target,
 * because that is the one drawing operation whose semantics fit in five lines
 * and it is what makes a frame visibly change; the DrawPrimitive family
 * accepts its vertices and counts them. Transform, light and material state is
 * stored and never used. Rasterising is the next job and it is a big one --
 * this is the layer that has to exist first so the game gets that far.
 */
#define D3DDEV_SLOTS 49

#define D3D_MAXRS      256      /* render states the game may set */
#define D3D_MAXSTAGE   8
#define D3D_MAXTSS     32
#define D3D_MAXLIGHT   16

static uint32_t g_d3d_rs[D3D_MAXRS];
static uint32_t g_d3d_tss[D3D_MAXSTAGE][D3D_MAXTSS];
static uint32_t g_d3d_tex[D3D_MAXSTAGE];
static uint8_t  g_d3d_lighton[D3D_MAXLIGHT];
static float    g_d3d_xf[8][16];                /* world/view/projection/... */
static uint32_t g_d3d_viewport[6];              /* x, y, w, h, minz, maxz */

static void d3ddev_QueryInterface(void) {
    /* The game asks a device for IID_IDirect3DDevice7 to confirm what it has. */
    if (ARG(2)) MEM32(ARG(2)) = ARG(0);
    O_REF(ARG(0))++;
    RET(DD_OK); STDRET(3);
}

static void d3ddev_GetCaps(void) {
    /* The same description that was enumerated. A device whose caps differ
     * from the ones it was chosen for is how a renderer ends up asking for
     * something this layer never offered. */
    if (ARG(1) && g_d3d_devdesc)
        memcpy((void*)(uintptr_t)ADDR(ARG(1)),
               (void*)(uintptr_t)ADDR(g_d3d_devdesc), sizeof(D3DDEVICEDESC7));
    RET(DD_OK); STDRET(2);
}

/*
 * EnumTextureFormats(callback, context).
 *
 * The callback takes a DDPIXELFORMAT, and returning without calling it means
 * "this device can draw no textures", which is not an answer any renderer can
 * work with. Four formats: 565 opaque, 1555 and 4444 for alpha, and 8888.
 */
static void d3ddev_EnumTextureFormats(void) {
    uint32_t cb = ARG(1), ctx = ARG(2);
    recomp_func_t f = cb ? recomp_lookup(cb) : NULL;
    if (!f) { RET(DD_OK); STDRET(3); return; }

    static const struct { uint32_t flags, bits, r, g, b, a; } fmts[] = {
        {0x40,       16, 0xF800,     0x07E0,     0x001F,     0},           /* RGB 565  */
        {0x40|0x1,   16, 0x7C00,     0x03E0,     0x001F,     0x8000},      /* ARGB 1555 */
        {0x40|0x1,   16, 0x0F00,     0x00F0,     0x000F,     0xF000},      /* ARGB 4444 */
        {0x40,       32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0},           /* RGB 888  */
        {0x40|0x1,   32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000},  /* ARGB 8888 */
    };
    uint32_t pf = crt_alloc(32);
    for (unsigned i = 0; i < sizeof fmts / sizeof fmts[0]; i++) {
        for (uint32_t k = 0; k < 32; k += 4) MEM32(pf + k) = 0;
        MEM32(pf + 0x00) = 32;               /* dwSize */
        MEM32(pf + 0x04) = fmts[i].flags;    /* DDPF_RGB | DDPF_ALPHAPIXELS */
        MEM32(pf + 0x0C) = fmts[i].bits;     /* dwRGBBitCount */
        MEM32(pf + 0x10) = fmts[i].r;
        MEM32(pf + 0x14) = fmts[i].g;
        MEM32(pf + 0x18) = fmts[i].b;
        MEM32(pf + 0x1C) = fmts[i].a;

        uint32_t save = g_esp, save_fn = g_cur_func;
        PUSH32(g_esp, ctx);
        PUSH32(g_esp, pf);
        PUSH32(g_esp, RECOMP_RETADDR);
        f();
        g_cur_func = save_fn;
        if (g_esp != save) g_esp = save;
        if (g_eax == 0) break;               /* D3DENUMRET_CANCEL */
    }
    RET(DD_OK); STDRET(3);
}

static void d3ddev_BeginScene(void) { RET(DD_OK); STDRET(1); }
static void d3ddev_EndScene(void)   { RET(DD_OK); STDRET(1); }

static void d3ddev_GetDirect3D(void) {
    if (ARG(1)) MEM32(ARG(1)) = obj_new(ddraw_d3d_vtable(), KIND_DD);
    RET(DD_OK); STDRET(2);
}
static void d3ddev_SetRenderTarget(void) {
    g_d3d_rt = ARG(1);
    RET(DD_OK); STDRET(3);
}
static void d3ddev_GetRenderTarget(void) {
    if (ARG(1)) MEM32(ARG(1)) = g_d3d_rt;
    if (g_d3d_rt) O_REF(g_d3d_rt)++;
    RET(DD_OK); STDRET(2);
}

/*
 * Clear(count, rects, flags, colour, z, stencil) -- and this one is real.
 *
 * D3DCLEAR_TARGET is 1. The colour arrives as 0x00RRGGBB and the render target
 * is 565 in the mode the game picked, so it is converted here the same way
 * present_surface converts the other way. Honouring the rectangle list is not
 * worth it: RE3D clears the whole target.
 */
/*
 * Clear(dwCount, lpRects, dwFlags, dwColor, dvZ, dwStencil).
 *
 * D3DCLEAR_TARGET is 1 and D3DCLEAR_ZBUFFER is 2, and the second one matters
 * as much as the first: a depth buffer that is never reset holds the previous
 * frame's depths and the next frame's geometry fails its own test.
 *
 * ponytail: the rectangle list is ignored and the whole surface is cleared.
 * The game passes count 0 (meaning the whole viewport) everywhere so far.
 */
static void d3ddev_Clear(void) {
    uint32_t flags = ARG(3), colour = ARG(4);
    uint32_t s = g_d3d_rt;
    if (flags & 2u) {
        uint32_t z = s ? O_ATTACH(s) : 0;
        if (z && (O_CAPS(z) & 0x20000u) && O_BITS(z)) {
            /* dvZ is a float in [0,1]; ARG(5) is its bit pattern. */
            uint32_t bits = ARG(5);
            float dz;
            memcpy(&dz, &bits, 4);
            if (dz < 0.0f) dz = 0.0f; else if (dz > 1.0f) dz = 1.0f;
            uint16_t zv = (uint16_t)(dz * 65535.0f);
            uint8_t* zb = (uint8_t*)(uintptr_t)ADDR(O_BITS(z));
            for (uint32_t y = 0; y < O_H(z); y++) {
                uint16_t* row = (uint16_t*)(zb + (size_t)y * O_PITCH(z));
                for (uint32_t x = 0; x < O_W(z); x++) row[x] = zv;
            }
        }
    }
    if ((flags & 1u) && s && O_BITS(s)) {
        uint32_t w = O_W(s), h = O_H(s), pitch = O_PITCH(s);
        uint8_t* base = (uint8_t*)(uintptr_t)ADDR(O_BITS(s));
        if (O_BPP(s) == 16) {
            uint16_t c = (uint16_t)(((colour >> 8) & 0xF800u)
                                  | ((colour >> 5) & 0x07E0u)
                                  | ((colour >> 3) & 0x001Fu));
            for (uint32_t y = 0; y < h; y++) {
                uint16_t* row = (uint16_t*)(base + (size_t)y * pitch);
                for (uint32_t x = 0; x < w; x++) row[x] = c;
            }
        } else if (O_BPP(s) == 32) {
            for (uint32_t y = 0; y < h; y++) {
                uint32_t* row = (uint32_t*)(base + (size_t)y * pitch);
                for (uint32_t x = 0; x < w; x++) row[x] = colour;
            }
        }
    }
    RET(DD_OK); STDRET(7);
}

static void d3ddev_SetTransform(void) {
    uint32_t st = ARG(1), m = ARG(2);
    if (m && st < 8)
        memcpy(g_d3d_xf[st], (void*)(uintptr_t)ADDR(m), 64);
    RET(DD_OK); STDRET(3);
}
static void d3ddev_GetTransform(void) {
    uint32_t st = ARG(1), m = ARG(2);
    if (m && st < 8)
        memcpy((void*)(uintptr_t)ADDR(m), g_d3d_xf[st], 64);
    RET(DD_OK); STDRET(3);
}
static void d3ddev_MultiplyTransform(void) { RET(DD_OK); STDRET(3); }

static void d3ddev_SetViewport(void) {
    uint32_t v = ARG(1);
    if (v) for (int i = 0; i < 6; i++) g_d3d_viewport[i] = MEM32(v + i * 4);
    RET(DD_OK); STDRET(2);
}
static void d3ddev_GetViewport(void) {
    uint32_t v = ARG(1);
    if (v) for (int i = 0; i < 6; i++) MEM32(v + i * 4) = g_d3d_viewport[i];
    RET(DD_OK); STDRET(2);
}

static void d3ddev_SetMaterial(void) { RET(DD_OK); STDRET(2); }
static void d3ddev_GetMaterial(void) {
    /* D3DMATERIAL7 is 16 floats plus a power; a getter that leaves it alone
     * gives the caller stack contents to multiply colours by. */
    if (ARG(1)) for (int i = 0; i < 0x44; i += 4) MEM32(ARG(1) + i) = 0;
    RET(DD_OK); STDRET(2);
}
static void d3ddev_SetLight(void) { RET(DD_OK); STDRET(3); }
static void d3ddev_GetLight(void) {
    if (ARG(2)) for (int i = 0; i < 0x68; i += 4) MEM32(ARG(2) + i) = 0;
    RET(DD_OK); STDRET(3);
}

static void d3ddev_SetRenderState(void) {
    if (ARG(1) < D3D_MAXRS) g_d3d_rs[ARG(1)] = ARG(2);
    RET(DD_OK); STDRET(3);
}
static void d3ddev_GetRenderState(void) {
    if (ARG(2)) MEM32(ARG(2)) = (ARG(1) < D3D_MAXRS) ? g_d3d_rs[ARG(1)] : 0;
    RET(DD_OK); STDRET(3);
}

static void d3ddev_BeginStateBlock(void) { RET(DD_OK); STDRET(1); }
static void d3ddev_EndStateBlock(void) {
    static uint32_t next = 1;
    if (ARG(1)) MEM32(ARG(1)) = next++;
    RET(DD_OK); STDRET(2);
}
static void d3ddev_PreLoad(void) { RET(DD_OK); STDRET(2); }

/* ------------------------------------------------------- the DrawPrimitive
 * family, rasterised.
 *
 * raster.c does the triangles; this is the part that has to know where the
 * game's data is. Three things have to be assembled per batch:
 *
 *   the render target   the surface CreateDevice was given, whose bits are
 *                       ordinary target memory
 *   the texture         stage 0's surface, or none
 *   the transform       world * view * projection, needed only when the FVF
 *                       carries XYZ rather than XYZRHW
 *
 * Force Commander's front end calls DrawIndexedPrimitiveVB and nothing else so
 * far, but all four unstrided entry points are wired because the difference
 * between them is only where the vertices come from.
 *
 * ponytail: the strided entry points still only count. Their
 * D3DDRAWPRIMITIVESTRIDEDDATA is a separate pointer and stride per vertex
 * element, which needs its own gather loop; nothing has called them.
 */
static void rs_from_surface(rsurf_t* o, uint32_t s) {
    if (!s || !O_BITS(s)) { memset(o, 0, sizeof *o); return; }
    o->bits = (uint8_t*)(uintptr_t)ADDR(O_BITS(s));
    o->w = (int)O_W(s);
    o->h = (int)O_H(s);
    o->pitch = (int)O_PITCH(s);
    o->bpp = (int)O_BPP(s);
    o->pf = (int)O_PF(s);
}

/* row-major 4x4 multiply, a then b */
static void mat_mul(float* o, const float* a, const float* b) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++) v += a[r * 4 + k] * b[k * 4 + c];
            o[r * 4 + c] = v;
        }
}

static void d3d_rasterise(uint32_t prim, uint32_t fvf, uint32_t verts,
                          uint32_t nvert, uint32_t indices, uint32_t nidx) {
    g_d3d_prims++;
    uint32_t rt = g_d3d_rt ? g_d3d_rt : (g_primary ? O_BACK(g_primary) : 0);
    if (!rt || !verts || !nvert) return;

    rsurf_t target, tex, zbuf;
    rs_from_surface(&target, rt);
    rs_from_surface(&tex, g_d3d_tex[0]);
    /* The depth buffer is the surface the game attached to the render target;
     * DDSCAPS_ZBUFFER is 0x20000. */
    uint32_t zs = O_ATTACH(rt);
    rs_from_surface(&zbuf, (zs && (O_CAPS(zs) & 0x20000u)) ? zs : 0);
    if (!target.bits) return;

    /* The pixel state the game is actually using, on the first few draws.
     * Every one of these mattered: blend/src/dst said which blend mode to
     * implement, atest/afunc/aref said the front end relies on the alpha test,
     * and the texture stage ops said alpha comes from the texture. */
    { static unsigned n;
      if (n++ < 3)
          fprintf(stderr, "[d3d] draw prim=%u fvf=0x%X nv=%u ni=%u"
                          " tex=0x%08X/%ubpp/pf%u blend=%u src=%u dst=%u"
                          " atest=%u afunc=%u aref=%u cull=%u zen=%u zw=%u"
                          " zf=%u ckey=%u tss=%u,%u,%u/%u,%u,%u\n",
                  prim, fvf, nvert, nidx, g_d3d_tex[0],
                  g_d3d_tex[0] ? O_BPP(g_d3d_tex[0]) : 0,
                  g_d3d_tex[0] ? O_PF(g_d3d_tex[0]) : 0,
                  g_d3d_rs[27], g_d3d_rs[19], g_d3d_rs[20], g_d3d_rs[15],
                  g_d3d_rs[25], g_d3d_rs[24], g_d3d_rs[22], g_d3d_rs[7],
                  g_d3d_rs[14], g_d3d_rs[23], g_d3d_rs[41],
                  g_d3d_tss[0][1], g_d3d_tss[0][2], g_d3d_tss[0][3],
                  g_d3d_tss[0][4], g_d3d_tss[0][5], g_d3d_tss[0][6]); }

    float wv[16], wvp[16];
    mat_mul(wv, g_d3d_xf[1], g_d3d_xf[2]);      /* world * view */
    mat_mul(wvp, wv, g_d3d_xf[3]);              /* ... * projection */

    int vp[4] = {(int)g_d3d_viewport[0], (int)g_d3d_viewport[1],
                 (int)g_d3d_viewport[2], (int)g_d3d_viewport[3]};

    /* D3DRENDERSTATE_ALPHABLENDENABLE 27, SRCBLEND 19, DESTBLEND 20,
     * ALPHATESTENABLE 15, ALPHAFUNC 25, ALPHAREF 24. D3DBLEND_SRCALPHA is 5
     * and INVSRCALPHA is 6; D3DCMP_GREATER is 5, which with a reference of 0
     * is "draw anything that is not fully transparent". ZENABLE is 7,
     * ZWRITEENABLE 14, ZFUNC 23. */
    rstate_t st;
    st.blend = (g_d3d_rs[27] && g_d3d_rs[19] == 5 && g_d3d_rs[20] == 6);
    st.alpha_test = (g_d3d_rs[15] && g_d3d_rs[25] == 5);
    st.alpha_ref = (int)(g_d3d_rs[24] & 0xFF);
    st.z_test = (g_d3d_rs[7] != 0);
    st.z_write = (g_d3d_rs[14] != 0);
    st.z_func = g_d3d_rs[23] ? (int)g_d3d_rs[23] : 4;

    g_d3d_pixels += raster_draw(&target, zbuf.bits ? &zbuf : NULL,
                                tex.bits ? &tex : NULL, (int)prim,
                                fvf, (const uint8_t*)(uintptr_t)ADDR(verts),
                                nvert,
                                indices ? (const uint16_t*)(uintptr_t)ADDR(indices)
                                        : NULL,
                                nidx, wvp, vp, &st);
}

/* DrawPrimitive(type, fvf, verts, count, flags) */
static void d3ddev_DrawPrimitive(void) {
    d3d_rasterise(ARG(1), ARG(2), ARG(3), ARG(4), 0, 0);
    RET(DD_OK); STDRET(6);
}

/* DrawIndexedPrimitive(type, fvf, verts, vcount, indices, icount, flags) */
static void d3ddev_DrawIndexedPrimitive(void) {
    d3d_rasterise(ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6));
    RET(DD_OK); STDRET(8);
}

/* DrawPrimitiveVB(type, vb, startVertex, numVertices, flags).
 *
 * The vertices are in OUR vertex buffer, written by the game through Lock at
 * its own FVF stride -- VB_STRIDE is only how much was allocated. */
static void d3ddev_DrawPrimitiveVB(void) {
    uint32_t vb = ARG(2), start = ARG(3), n = ARG(4);
    if (vb) {
        uint32_t fvf = VB_FVF(vb), st = raster_stride(fvf);
        d3d_rasterise(ARG(1), fvf, VB_DATA(vb) + start * st, n, 0, 0);
    } else g_d3d_prims++;
    RET(DD_OK); STDRET(6);
}

/* DrawIndexedPrimitiveVB(type, vb, startVertex, numVertices,
 *                        indices, icount, flags) */
static void d3ddev_DrawIndexedPrimitiveVB(void) {
    uint32_t vb = ARG(2), start = ARG(3), n = ARG(4);
    if (vb) {
        uint32_t fvf = VB_FVF(vb), st = raster_stride(fvf);
        d3d_rasterise(ARG(1), fvf, VB_DATA(vb) + start * st, n,
                      ARG(5), ARG(6));
    } else g_d3d_prims++;
    RET(DD_OK); STDRET(8);
}

/* Still only counted: see the note above. */
static void d3ddev_draw5(void) { g_d3d_prims++; RET(DD_OK); STDRET(6); }
static void d3ddev_draw7(void) { g_d3d_prims++; RET(DD_OK); STDRET(8); }

static void d3ddev_SetClipStatus(void) { RET(DD_OK); STDRET(2); }
static void d3ddev_GetClipStatus(void) {
    /* D3DCLIPSTATUS: dwFlags, dwStatus, then six floats of extents. */
    if (ARG(1)) for (int i = 0; i < 0x20; i += 4) MEM32(ARG(1) + i) = 0;
    RET(DD_OK); STDRET(2);
}
static void d3ddev_ComputeSphereVisibility(void) {
    /* One DWORD per sphere; 0 means "wholly inside the frustum". */
    uint32_t n = ARG(2), out = ARG(4);
    if (out) for (uint32_t i = 0; i < n; i++) MEM32(out + i * 4) = 0;
    RET(DD_OK); STDRET(5);
}

static void d3ddev_GetTexture(void) {
    if (ARG(2)) MEM32(ARG(2)) = (ARG(1) < D3D_MAXSTAGE) ? g_d3d_tex[ARG(1)] : 0;
    RET(DD_OK); STDRET(3);
}
static void d3ddev_SetTexture(void) {
    if (ARG(1) < D3D_MAXSTAGE) g_d3d_tex[ARG(1)] = ARG(2);
    RET(DD_OK); STDRET(3);
}
static void d3ddev_GetTextureStageState(void) {
    uint32_t st = ARG(1), k = ARG(2);
    if (ARG(3))
        MEM32(ARG(3)) = (st < D3D_MAXSTAGE && k < D3D_MAXTSS)
                      ? g_d3d_tss[st][k] : 0;
    RET(DD_OK); STDRET(4);
}
static void d3ddev_SetTextureStageState(void) {
    uint32_t st = ARG(1), k = ARG(2);
    if (st < D3D_MAXSTAGE && k < D3D_MAXTSS) g_d3d_tss[st][k] = ARG(3);
    RET(DD_OK); STDRET(4);
}
static void d3ddev_ValidateDevice(void) {
    if (ARG(1)) MEM32(ARG(1)) = 1;            /* one pass, no extra work */
    RET(DD_OK); STDRET(2);
}
static void d3ddev_stateblock1(void) { RET(DD_OK); STDRET(2); }
static void d3ddev_CreateStateBlock(void) {
    static uint32_t next = 0x100;
    if (ARG(2)) MEM32(ARG(2)) = next++;
    RET(DD_OK); STDRET(3);
}
/*
 * IDirect3DDevice7::Load(destTex, destPoint, srcTex, srcRect, flags).
 *
 * This was a no-op, and it is how every texture in the game stayed black.
 *
 * The upload path is a PAIR of surfaces -- the trace shows them created back
 * to back:
 *
 *   [dd] CreateSurface 256x256 32bpp caps=0x4401008   texture, video memory
 *   [dd] CreateSurface 256x256 32bpp caps=0x401808    texture, system memory
 *
 * The game locks the system-memory one, writes the image into it, and then
 * calls Load to move it to the one it draws with. With Load doing nothing the
 * video-memory texture kept the zeros it was allocated with, so the rasteriser
 * modulated every pixel by black and 3.4 billion rasterised pixels came out
 * invisible.
 *
 * ponytail: the top level only. Load is defined to walk the whole mip chain
 * and colour-key/palette-convert on the way; the attached-surface chain is
 * there (O_ATTACH) to walk when a mip level is ever sampled.
 */
static void d3ddev_Load(void) {
    uint32_t dst = ARG(1), dp = ARG(2), src = ARG(3), sr = ARG(4);
    int dx = 0, dy = 0, sx = 0, sy = 0;
    int w = src ? (int)O_W(src) : 0, h = src ? (int)O_H(src) : 0;
    if (dp) { dx = (int)MEM32(dp); dy = (int)MEM32(dp + 4); }
    if (sr) { sx = (int)MEM32(sr); sy = (int)MEM32(sr + 4);
              w = (int)MEM32(sr + 8) - sx; h = (int)MEM32(sr + 12) - sy; }
    if (dst && src) {
        blit(dst, dx, dy, src, sx, sy, w, h);
        static unsigned n;
        if (n++ < 12) {
            fprintf(stderr, "[d3d] Load 0x%08X <- 0x%08X %dx%d at %d,%d\n",
                    dst, src, w, h, dx, dy);
            if (g_dump_path) {
                char path[512];
                snprintf(path, sizeof path, "%s.tex%u.bmp", g_dump_path, n);
                ddraw_dump_surface(dst, path);
            }
        }
    }
    RET(DD_OK); STDRET(6);
}
static void d3ddev_LightEnable(void) {
    if (ARG(1) < D3D_MAXLIGHT) g_d3d_lighton[ARG(1)] = (uint8_t)(ARG(2) != 0);
    RET(DD_OK); STDRET(3);
}
static void d3ddev_GetLightEnable(void) {
    if (ARG(2))
        MEM32(ARG(2)) = (ARG(1) < D3D_MAXLIGHT) ? g_d3d_lighton[ARG(1)] : 0;
    RET(DD_OK); STDRET(3);
}
static void d3ddev_SetClipPlane(void) { RET(DD_OK); STDRET(3); }
static void d3ddev_GetClipPlane(void) {
    if (ARG(2)) for (int i = 0; i < 16; i += 4) MEM32(ARG(2) + i) = 0;
    RET(DD_OK); STDRET(3);
}
static void d3ddev_GetInfo(void) { RET(DDERR_UNSUPPORTED); STDRET(4); }

static void d3d_init(void) {
    static const vtent_t d3d[] = {
        {m_QueryInterface,        "IDirect3D7::QueryInterface"},
        {m_AddRef,                "IDirect3D7::AddRef"},
        {m_Release,               "IDirect3D7::Release"},
        {d3d_EnumDevices,         "IDirect3D7::EnumDevices"},
        {d3d_CreateDevice,        "IDirect3D7::CreateDevice"},
        {d3d_CreateVertexBuffer,  "IDirect3D7::CreateVertexBuffer"},
        {d3d_EnumZBufferFormats,  "IDirect3D7::EnumZBufferFormats"},
        {d3d_EvictManagedTextures,"IDirect3D7::EvictManagedTextures"},
    };
    g_vtbl_d3d = build_vtable(d3d, sizeof(d3d) / sizeof(d3d[0]));

    static const vtent_t dev[D3DDEV_SLOTS] = {
        {d3ddev_QueryInterface,      "IDirect3DDevice7::QueryInterface"},
        {m_AddRef,                   "IDirect3DDevice7::AddRef"},
        {m_Release,                  "IDirect3DDevice7::Release"},
        {d3ddev_GetCaps,             "IDirect3DDevice7::GetCaps"},
        {d3ddev_EnumTextureFormats,  "IDirect3DDevice7::EnumTextureFormats"},
        {d3ddev_BeginScene,          "IDirect3DDevice7::BeginScene"},
        {d3ddev_EndScene,            "IDirect3DDevice7::EndScene"},
        {d3ddev_GetDirect3D,         "IDirect3DDevice7::GetDirect3D"},
        {d3ddev_SetRenderTarget,     "IDirect3DDevice7::SetRenderTarget"},
        {d3ddev_GetRenderTarget,     "IDirect3DDevice7::GetRenderTarget"},
        {d3ddev_Clear,               "IDirect3DDevice7::Clear"},
        {d3ddev_SetTransform,        "IDirect3DDevice7::SetTransform"},
        {d3ddev_GetTransform,        "IDirect3DDevice7::GetTransform"},
        {d3ddev_SetViewport,         "IDirect3DDevice7::SetViewport"},
        {d3ddev_MultiplyTransform,   "IDirect3DDevice7::MultiplyTransform"},
        {d3ddev_GetViewport,         "IDirect3DDevice7::GetViewport"},
        {d3ddev_SetMaterial,         "IDirect3DDevice7::SetMaterial"},
        {d3ddev_GetMaterial,         "IDirect3DDevice7::GetMaterial"},
        {d3ddev_SetLight,            "IDirect3DDevice7::SetLight"},
        {d3ddev_GetLight,            "IDirect3DDevice7::GetLight"},
        {d3ddev_SetRenderState,      "IDirect3DDevice7::SetRenderState"},
        {d3ddev_GetRenderState,      "IDirect3DDevice7::GetRenderState"},
        {d3ddev_BeginStateBlock,     "IDirect3DDevice7::BeginStateBlock"},
        {d3ddev_EndStateBlock,       "IDirect3DDevice7::EndStateBlock"},
        {d3ddev_PreLoad,             "IDirect3DDevice7::PreLoad"},
        {d3ddev_DrawPrimitive,       "IDirect3DDevice7::DrawPrimitive"},
        {d3ddev_DrawIndexedPrimitive, "IDirect3DDevice7::DrawIndexedPrimitive"},
        {d3ddev_SetClipStatus,       "IDirect3DDevice7::SetClipStatus"},
        {d3ddev_GetClipStatus,       "IDirect3DDevice7::GetClipStatus"},
        {d3ddev_draw5,               "IDirect3DDevice7::DrawPrimitiveStrided"},
        {d3ddev_draw7,               "IDirect3DDevice7::DrawIndexedPrimitiveStrided"},
        {d3ddev_DrawPrimitiveVB,     "IDirect3DDevice7::DrawPrimitiveVB"},
        {d3ddev_DrawIndexedPrimitiveVB, "IDirect3DDevice7::DrawIndexedPrimitiveVB"},
        {d3ddev_ComputeSphereVisibility, "IDirect3DDevice7::ComputeSphereVisibility"},
        {d3ddev_GetTexture,          "IDirect3DDevice7::GetTexture"},
        {d3ddev_SetTexture,          "IDirect3DDevice7::SetTexture"},
        {d3ddev_GetTextureStageState,"IDirect3DDevice7::GetTextureStageState"},
        {d3ddev_SetTextureStageState,"IDirect3DDevice7::SetTextureStageState"},
        {d3ddev_ValidateDevice,      "IDirect3DDevice7::ValidateDevice"},
        {d3ddev_stateblock1,         "IDirect3DDevice7::ApplyStateBlock"},
        {d3ddev_stateblock1,         "IDirect3DDevice7::CaptureStateBlock"},
        {d3ddev_stateblock1,         "IDirect3DDevice7::DeleteStateBlock"},
        {d3ddev_CreateStateBlock,    "IDirect3DDevice7::CreateStateBlock"},
        {d3ddev_Load,                "IDirect3DDevice7::Load"},
        {d3ddev_LightEnable,         "IDirect3DDevice7::LightEnable"},
        {d3ddev_GetLightEnable,      "IDirect3DDevice7::GetLightEnable"},
        {d3ddev_SetClipPlane,        "IDirect3DDevice7::SetClipPlane"},
        {d3ddev_GetClipPlane,        "IDirect3DDevice7::GetClipPlane"},
        {d3ddev_GetInfo,             "IDirect3DDevice7::GetInfo"},
    };
    g_vtbl_d3ddev = build_vtable(dev, D3DDEV_SLOTS);
}

uint32_t ddraw_d3d_vtable(void) {
    if (!g_vtbl_d3d) d3d_init();
    return g_vtbl_d3d;
}
