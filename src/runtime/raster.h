/* A software rasteriser for the Direct3D 7 DrawPrimitive family.
 *
 * Deliberately free of the recomp runtime: it takes host pointers and plain
 * integers, so it compiles and runs on its own (see RASTER_MAIN in raster.c)
 * and the caller owns every target-address translation.
 */
#ifndef RASTER_H
#define RASTER_H

#include <stdint.h>

/*
 * Pixel formats, because a bit depth is not a format.
 *
 * The 128x128 font atlas is created as ARGB1555 (R=0x7C00, G=0x03E0,
 * B=0x001F, A=0x8000) and decoding it as 565 shifts every channel. The 32-bit
 * textures come in two flavours and only one of them has a meaningful high
 * byte: a surface created with an alpha mask of 0 is XRGB, and reading its top
 * byte as alpha is reading whatever the game left there.
 */
#define RPF_UNKNOWN   0
#define RPF_RGB565    1
#define RPF_ARGB1555  2
#define RPF_ARGB4444  3
#define RPF_XRGB8888  4
#define RPF_ARGB8888  5

typedef struct {
    uint8_t* bits;
    int w, h, pitch, bpp;
    int pf;                    /* RPF_*; RPF_UNKNOWN falls back to the bpp */
} rsurf_t;

/* D3DPRIMITIVETYPE */
#define RP_POINTLIST     1
#define RP_LINELIST      2
#define RP_LINESTRIP     3
#define RP_TRIANGLELIST  4
#define RP_TRIANGLESTRIP 5
#define RP_TRIANGLEFAN   6

/* The FVF bits this understands. */
#define RFVF_XYZ      0x002
#define RFVF_XYZRHW   0x004
#define RFVF_NORMAL   0x010
#define RFVF_DIFFUSE  0x040
#define RFVF_SPECULAR 0x080
#define RFVF_TEXMASK  0xF00
#define RFVF_TEXSHIFT 8

/*
 * The pixel state that changes what a triangle looks like. NULL means opaque
 * with no alpha test, which is what the self-test uses.
 *
 * Only one blend mode is implemented, because it is the only one Force
 * Commander's front end sets: SRCBLEND = SRCALPHA, DESTBLEND = INVSRCALPHA.
 * Any other pair falls back to opaque rather than guessing.
 */
typedef struct {
    int blend;          /* 1 = src*a + dst*(1-a) */
    int alpha_test;     /* 1 = discard a pixel whose alpha <= alpha_ref */
    int alpha_ref;
    int z_test;         /* 1 = compare against the depth buffer */
    int z_write;        /* 1 = store the depth of a pixel that passed */
    int z_func;         /* D3DCMP_*: 1 never .. 8 always, 4 = lessequal */
} rstate_t;

/* Bytes per vertex for an FVF, or 0 if the position format is unknown. */
uint32_t raster_stride(uint32_t fvf);

/* One pixel of a surface as 0x00RRGGBB, for a caller that wants to know
 * whether a draw changed it. */
uint32_t raster_peek(const rsurf_t* s, int x, int y);

/*
 * Draw one primitive batch.
 *
 *   tex      NULL or tex->bits == NULL for untextured
 *   idx      NULL for a sequential (non-indexed) draw
 *   wvp      world*view*projection, row-major, or NULL when the FVF carries
 *            XYZRHW and the positions are already in screen space
 *   vp       viewport x, y, w, h
 *
 * Returns the number of pixels written, which is what a caller logs to find
 * out whether a frame had anything in it.
 */
uint32_t raster_draw(const rsurf_t* rt, const rsurf_t* z, const rsurf_t* tex,
                     int prim, uint32_t fvf, const uint8_t* v, uint32_t nvert,
                     const uint16_t* idx, uint32_t nidx,
                     const float* wvp, const int* vp, const rstate_t* st);

#endif
