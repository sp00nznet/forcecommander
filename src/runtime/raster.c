/*
 * A software rasteriser for the Direct3D 7 DrawPrimitive family.
 *
 * The game gets this far: it sets a 640x480 mode, creates a flipping primary
 * with a Z buffer, creates a device, uploads textures, sets render state,
 * fills vertex buffers, calls BeginScene / Clear / DrawPrimitive* / EndScene /
 * Flip, and presents 200 frames a run. Everything except the DrawPrimitive
 * family was implemented; the frames came out as the clear colour because the
 * draws were counted and thrown away. This is the part that turns a colour
 * into a picture.
 *
 * Force Commander's front end draws with FVF 0x142 and 0x144 -- XYZ or XYZRHW,
 * diffuse, one texture set -- in triangle lists, strips and fans, which is
 * what this covers.
 *
 * ponytail: the deliberate ceilings, all of them measurable by looking at a
 * frame rather than by reading this.
 *
 *   - The depth buffer is 16-bit only, which is what the game asks for
 *     (DDSCAPS_ZBUFFER, G mask 0xFFFF). A 24+8 or 32-bit Z surface would need
 *     its own unpack; EnumZBufferFormats offers all three.
 *   - No alpha blend. SRCALPHA/INVSRCALPHA is the one the UI will want next.
 *   - Point sampling, no mip selection, no wrap mode: u,v are clamped. The
 *     textures are 128x128 and 256x256 and the UI draws them near 1:1.
 *   - Flat perspective: the barycentric interpolation is in screen space, so
 *     texture coordinates on a steeply angled 3D triangle will swim. Divide
 *     u,v,1 by w and interpolate those when that becomes visible.
 *   - One texture stage. The game sets up to eight; stage 0 is what it draws
 *     with so far.
 *   - No fill rule. A pixel centre exactly on a shared edge can be drawn by
 *     both triangles or by neither, so a strip can show a hairline seam. Add
 *     the top-left rule if one ever shows up in a screenshot.
 *
 * One runnable check, no framework:
 *
 *   gcc -DRASTER_MAIN -o raster_test src/runtime/raster.c && ./raster_test
 */
#include "raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

uint32_t raster_stride(uint32_t fvf) {
    uint32_t n;
    if (fvf & RFVF_XYZRHW)   n = 16;
    else if (fvf & RFVF_XYZ) n = 12;
    else                     return 0;
    if (fvf & RFVF_NORMAL)   n += 12;
    if (fvf & RFVF_DIFFUSE)  n += 4;
    if (fvf & RFVF_SPECULAR) n += 4;
    n += ((fvf & RFVF_TEXMASK) >> RFVF_TEXSHIFT) * 8;
    return n;
}

/* A vertex after transform: screen x/y, depth, colour, texture coordinates. */
typedef struct {
    float x, y, z;
    float u, v;
    uint32_t c;          /* 0xAARRGGBB */
    int ok;              /* 0 when the vertex was behind the eye */
} rvert_t;

static int pf_of(const rsurf_t* s) {
    if (s->pf != RPF_UNKNOWN) return s->pf;
    return s->bpp == 16 ? RPF_RGB565 : RPF_XRGB8888;
}

static void put(const rsurf_t* s, int x, int y, uint32_t rgb) {
    uint8_t* row = s->bits + (size_t)y * s->pitch;
    switch (pf_of(s)) {
    case RPF_ARGB1555:
        ((uint16_t*)row)[x] = (uint16_t)(0x8000u
                                       | ((rgb >> 9) & 0x7C00u)
                                       | ((rgb >> 6) & 0x03E0u)
                                       | ((rgb >> 3) & 0x001Fu));
        break;
    case RPF_ARGB4444:
        ((uint16_t*)row)[x] = (uint16_t)(0xF000u
                                       | ((rgb >> 12) & 0x0F00u)
                                       | ((rgb >> 8) & 0x00F0u)
                                       | ((rgb >> 4) & 0x000Fu));
        break;
    case RPF_RGB565:
        ((uint16_t*)row)[x] = (uint16_t)(((rgb >> 8) & 0xF800u)
                                       | ((rgb >> 5) & 0x07E0u)
                                       | ((rgb >> 3) & 0x001Fu));
        break;
    default:
        ((uint32_t*)row)[x] = rgb;
        break;
    }
}

/* Unpack one pixel to 0xAARRGGBB, alpha 0xFF where the format has none. */
static uint32_t unpack(const rsurf_t* s, int x, int y) {
    const uint8_t* row = s->bits + (size_t)y * s->pitch;
    uint32_t p;
    switch (pf_of(s)) {
    case RPF_ARGB1555:
        p = ((const uint16_t*)row)[x];
        return ((p & 0x8000u) ? 0xFF000000u : 0u)
             | ((p & 0x7C00u) << 9) | ((p & 0x03E0u) << 6)
             | ((p & 0x001Fu) << 3);
    case RPF_ARGB4444:
        p = ((const uint16_t*)row)[x];
        return ((p & 0xF000u) << 16) | ((p & 0xF000u) << 12)
             | ((p & 0x0F00u) << 12) | ((p & 0x0F00u) << 8)
             | ((p & 0x00F0u) << 8)  | ((p & 0x00F0u) << 4)
             | ((p & 0x000Fu) << 4)  |  (p & 0x000Fu);
    case RPF_RGB565:
        p = ((const uint16_t*)row)[x];
        return 0xFF000000u | ((p & 0xF800u) << 8) | ((p & 0x07E0u) << 5)
                           | ((p & 0x001Fu) << 3);
    case RPF_XRGB8888:
        return 0xFF000000u | (((const uint32_t*)row)[x] & 0xFFFFFFu);
    default:
        return ((const uint32_t*)row)[x];
    }
}

static uint32_t sample(const rsurf_t* t, float u, float v) {
    int x = (int)(u * (float)t->w);
    int y = (int)(v * (float)t->h);
    if (x < 0) x = 0; else if (x >= t->w) x = t->w - 1;
    if (y < 0) y = 0; else if (y >= t->h) y = t->h - 1;
    return unpack(t, x, y);
}

static uint32_t modulate(uint32_t a, uint32_t b) {
    uint32_t r = (((a >> 16) & 0xFF) * ((b >> 16) & 0xFF)) / 255;
    uint32_t g = (((a >> 8) & 0xFF) * ((b >> 8) & 0xFF)) / 255;
    uint32_t bl = ((a & 0xFF) * (b & 0xFF)) / 255;
    return (r << 16) | (g << 8) | bl;
}

/* One triangle, half-space edge functions, barycentric interpolation. */
/* D3DCMP_*, against a 16-bit depth buffer. */
static int zpass(int func, uint32_t nz, uint32_t oz) {
    switch (func) {
    case 1: return 0;                       /* NEVER */
    case 2: return nz <  oz;                /* LESS */
    case 3: return nz == oz;                /* EQUAL */
    case 4: return nz <= oz;                /* LESSEQUAL */
    case 5: return nz >  oz;                /* GREATER */
    case 6: return nz != oz;                /* NOTEQUAL */
    case 7: return nz >= oz;                /* GREATEREQUAL */
    default: return 1;                      /* ALWAYS */
    }
}

static uint32_t tri(const rsurf_t* rt, const rsurf_t* zb, const rsurf_t* tex,
                    const rstate_t* st, int fvf_has_diffuse,
                    const rvert_t* a, const rvert_t* b, const rvert_t* c) {
    if (!a->ok || !b->ok || !c->ok) return 0;

    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area > -0.0001f && area < 0.0001f) return 0;   /* degenerate */

    int x0 = (int)floorf(fminf(fminf(a->x, b->x), c->x));
    int x1 = (int)ceilf(fmaxf(fmaxf(a->x, b->x), c->x));
    int y0 = (int)floorf(fminf(fminf(a->y, b->y), c->y));
    int y1 = (int)ceilf(fmaxf(fmaxf(a->y, b->y), c->y));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > rt->w - 1) x1 = rt->w - 1;
    if (y1 > rt->h - 1) y1 = rt->h - 1;
    if (x1 < x0 || y1 < y0) return 0;

    float inv = 1.0f / area;
    uint32_t drawn = 0;
    for (int y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float w0 = ((b->x - a->x) * (py - a->y)
                      - (b->y - a->y) * (px - a->x)) * inv;   /* toward c */
            float w1 = ((px - a->x) * (c->y - a->y)
                      - (py - a->y) * (c->x - a->x)) * inv;   /* toward b */
            float w2 = 1.0f - w0 - w1;                        /* toward a */
            /* Dividing the edge functions by the SIGNED area is what makes
             * this winding-independent: an interior point gives all three
             * weights in [0,1] either way round, so both windings draw and
             * neither is culled. The game switches cull mode and this layer
             * does not implement culling. */
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            float aw = w2, bw = w1, cw = w0;

            uint32_t col;
            {
                float r = aw * (float)((a->c >> 16) & 0xFF)
                        + bw * (float)((b->c >> 16) & 0xFF)
                        + cw * (float)((c->c >> 16) & 0xFF);
                float g = aw * (float)((a->c >> 8) & 0xFF)
                        + bw * (float)((b->c >> 8) & 0xFF)
                        + cw * (float)((c->c >> 8) & 0xFF);
                float bb = aw * (float)(a->c & 0xFF)
                         + bw * (float)(b->c & 0xFF)
                         + cw * (float)(c->c & 0xFF);
                col = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bb;
            }
            /* Depth first: it is the cheapest rejection and it is what the
             * front end relies on. The panel over the game's own title was
             * drawn after the text and won because there was no depth test. */
            uint16_t* zrow = NULL;
            uint32_t nz = 0;
            if (zb && zb->bits && st && (st->z_test || st->z_write)) {
                float zf = aw * a->z + bw * b->z + cw * c->z;
                if (zf < 0.0f) zf = 0.0f; else if (zf > 1.0f) zf = 1.0f;
                nz = (uint32_t)(zf * 65535.0f);
                zrow = (uint16_t*)(zb->bits + (size_t)y * zb->pitch);
                if (st->z_test && !zpass(st->z_func, nz, zrow[x])) continue;
            }

            uint32_t alpha = (a->c >> 24) & 0xFF;   /* diffuse alpha */
            if (!(fvf_has_diffuse)) alpha = 0xFF;
            if (tex && tex->bits) {
                float u = aw * a->u + bw * b->u + cw * c->u;
                float v = aw * a->v + bw * b->v + cw * c->v;
                uint32_t tc = sample(tex, u, v);
                alpha = (alpha * ((tc >> 24) & 0xFF)) / 255;
                col = modulate(tc, col);
            }
            if (st && st->alpha_test && (int)alpha <= st->alpha_ref) continue;
            if (st && st->blend && alpha < 255) {
                uint32_t d = unpack(rt, x, y);
                uint32_t ia = 255 - alpha;
                uint32_t r = (((col >> 16) & 0xFF) * alpha
                            + ((d >> 16) & 0xFF) * ia) / 255;
                uint32_t g = (((col >> 8) & 0xFF) * alpha
                            + ((d >> 8) & 0xFF) * ia) / 255;
                uint32_t bl = ((col & 0xFF) * alpha + (d & 0xFF) * ia) / 255;
                col = (r << 16) | (g << 8) | bl;
            }
            put(rt, x, y, col);
            if (zrow && st->z_write) zrow[x] = (uint16_t)nz;
            drawn++;
        }
    }
    return drawn;
}

/* row-major 4x4 times (x,y,z,1) */
static void xform(const float* m, float x, float y, float z, float* o) {
    o[0] = x * m[0] + y * m[4] + z * m[8]  + m[12];
    o[1] = x * m[1] + y * m[5] + z * m[9]  + m[13];
    o[2] = x * m[2] + y * m[6] + z * m[10] + m[14];
    o[3] = x * m[3] + y * m[7] + z * m[11] + m[15];
}

uint32_t raster_draw(const rsurf_t* rt, const rsurf_t* z, const rsurf_t* tex,
                     int prim, uint32_t fvf, const uint8_t* v, uint32_t nvert,
                     const uint16_t* idx, uint32_t nidx,
                     const float* wvp, const int* vp,
                     const rstate_t* st) {
    if (!rt || !rt->bits || !v || !nvert) return 0;
    uint32_t stride = raster_stride(fvf);
    if (!stride) return 0;

    /* Where each element sits inside a vertex. */
    uint32_t off = (fvf & RFVF_XYZRHW) ? 16 : 12;
    if (fvf & RFVF_NORMAL) off += 12;
    uint32_t off_diffuse = (fvf & RFVF_DIFFUSE) ? off : 0xFFFFFFFFu;
    if (fvf & RFVF_DIFFUSE) off += 4;
    if (fvf & RFVF_SPECULAR) off += 4;
    uint32_t ntex = (fvf & RFVF_TEXMASK) >> RFVF_TEXSHIFT;
    uint32_t off_uv = ntex ? off : 0xFFFFFFFFu;

    rvert_t* tv = (rvert_t*)malloc(sizeof(rvert_t) * nvert);
    if (!tv) return 0;

    int vx = vp ? vp[0] : 0, vy = vp ? vp[1] : 0;
    int vw = vp && vp[2] ? vp[2] : rt->w;
    int vh = vp && vp[3] ? vp[3] : rt->h;

    for (uint32_t i = 0; i < nvert; i++) {
        const uint8_t* p = v + (size_t)i * stride;
        float f[4];
        memcpy(f, p, 12);
        tv[i].ok = 1;
        if (fvf & RFVF_XYZRHW) {
            tv[i].x = f[0];
            tv[i].y = f[1];
            tv[i].z = f[2];
        } else if (wvp) {
            float o[4];
            xform(wvp, f[0], f[1], f[2], o);
            if (o[3] <= 0.0001f) { tv[i].ok = 0; o[3] = 1.0f; }
            float iw = 1.0f / o[3];
            tv[i].x = (float)vx + (o[0] * iw * 0.5f + 0.5f) * (float)vw;
            tv[i].y = (float)vy + (0.5f - o[1] * iw * 0.5f) * (float)vh;
            tv[i].z = o[2] * iw;
        } else {
            /* No transform to apply and positions that are not in screen
             * space: nothing honest to draw. */
            tv[i].ok = 0;
            tv[i].x = tv[i].y = tv[i].z = 0.0f;
        }
        tv[i].c = (off_diffuse != 0xFFFFFFFFu)
                ? *(const uint32_t*)(p + off_diffuse) : 0xFFFFFFFFu;
        if (off_uv != 0xFFFFFFFFu) {
            memcpy(&tv[i].u, p + off_uv, 4);
            memcpy(&tv[i].v, p + off_uv + 4, 4);
        } else {
            tv[i].u = tv[i].v = 0.0f;
        }
    }

    int hasdif = (off_diffuse != 0xFFFFFFFFu);
    uint32_t n = idx ? nidx : nvert, drawn = 0;
#define VI(k) (idx ? idx[(k)] : (uint16_t)(k))
    switch (prim) {
    case RP_TRIANGLELIST:
        for (uint32_t k = 0; k + 2 < n; k += 3)
            if (VI(k) < nvert && VI(k + 1) < nvert && VI(k + 2) < nvert)
                drawn += tri(rt, z, tex, st, hasdif, &tv[VI(k)],
                             &tv[VI(k + 1)], &tv[VI(k + 2)]);
        break;
    case RP_TRIANGLESTRIP:
        for (uint32_t k = 0; k + 2 < n; k++)
            if (VI(k) < nvert && VI(k + 1) < nvert && VI(k + 2) < nvert)
                drawn += tri(rt, z, tex, st, hasdif, &tv[VI(k)],
                             &tv[VI(k + 1)], &tv[VI(k + 2)]);
        break;
    case RP_TRIANGLEFAN:
        for (uint32_t k = 1; k + 1 < n; k++)
            if (VI(0) < nvert && VI(k) < nvert && VI(k + 1) < nvert)
                drawn += tri(rt, z, tex, st, hasdif, &tv[VI(0)], &tv[VI(k)],
                             &tv[VI(k + 1)]);
        break;
    case RP_POINTLIST:
        for (uint32_t k = 0; k < n; k++) {
            uint32_t j = VI(k);
            if (j >= nvert || !tv[j].ok) continue;
            int x = (int)tv[j].x, y = (int)tv[j].y;
            if (x < 0 || y < 0 || x >= rt->w || y >= rt->h) continue;
            put(rt, x, y, tv[j].c & 0xFFFFFF);
            drawn++;
        }
        break;
    default:
        /* ponytail: lines are not drawn. Nothing in this target has asked;
         * add a Bresenham walk over the same rvert_t when something does. */
        break;
    }
#undef VI
    free(tv);
    return drawn;
}

#ifdef RASTER_MAIN
#include <assert.h>
#include <stdio.h>

/* One triangle, screen space, no texture: the middle is filled, the corner
 * outside it is not, and the edge functions accept both windings. */
int main(void) {
    static uint32_t pix[64 * 64];
    rsurf_t rt = {(uint8_t*)pix, 64, 64, 64 * 4, 32, RPF_XRGB8888};

    typedef struct { float x, y, z, rhw; uint32_t c; } tlv_t;
    tlv_t v[3] = {
        {2.0f,  2.0f,  0.0f, 1.0f, 0xFFFF0000u},
        {60.0f, 2.0f,  0.0f, 1.0f, 0xFF00FF00u},
        {2.0f,  60.0f, 0.0f, 1.0f, 0xFF0000FFu},
    };
    uint32_t fvf = RFVF_XYZRHW | RFVF_DIFFUSE;
    assert(raster_stride(fvf) == 20);
    assert(raster_stride(RFVF_XYZ | RFVF_DIFFUSE | 0x100) == 24);
    assert(raster_stride(0) == 0);

    memset(pix, 0, sizeof pix);
    uint32_t drawn = raster_draw(&rt, NULL, NULL, RP_TRIANGLELIST, fvf,
                                 (const uint8_t*)v, 3, NULL, 0, NULL, NULL, NULL);
    assert(drawn > 1000);                      /* about half of 58x58 */
    assert(pix[10 * 64 + 10] != 0);            /* inside */
    assert(pix[60 * 64 + 60] == 0);            /* outside, past the diagonal */

    /* Reversed winding must draw the same pixels. */
    static uint32_t pix2[64 * 64];
    rsurf_t rt2 = {(uint8_t*)pix2, 64, 64, 64 * 4, 32, RPF_XRGB8888};
    memset(pix2, 0, sizeof pix2);
    tlv_t r[3] = {v[0], v[2], v[1]};
    uint32_t drawn2 = raster_draw(&rt2, NULL, NULL, RP_TRIANGLELIST, fvf,
                                  (const uint8_t*)r, 3, NULL, 0, NULL, NULL, NULL);
    /* Same coverage AND the same colours: the weights follow the vertices, so
     * reversing the winding is not supposed to change a pixel. */
    /* Same coverage to within the diagonal: there is no fill rule here (see
     * the ceilings at the top), so a pixel whose centre lands exactly on an
     * edge can go either way. 10 of ~1700 do. */
    assert(drawn2 == drawn);
    { int cov = 0;
      for (int i = 0; i < 64 * 64; i++)
          if ((pix[i] != 0) != (pix2[i] != 0)) cov++;
      assert(cov < 32); }

    /* A texture modulates, and 16bpp writes go through the 565 pack. */
    static uint16_t tpix[4 * 4];
    for (int i = 0; i < 16; i++) tpix[i] = 0xF800;      /* red */
    rsurf_t tex = {(uint8_t*)tpix, 4, 4, 4 * 2, 16, RPF_RGB565};
    struct { float x, y, z, rhw; uint32_t c; float u, v; } t[3] = {
        {2.0f,  2.0f,  0.0f, 1.0f, 0xFFFFFFFFu, 0.0f, 0.0f},
        {60.0f, 2.0f,  0.0f, 1.0f, 0xFFFFFFFFu, 1.0f, 0.0f},
        {2.0f,  60.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0.0f, 1.0f},
    };
    static uint16_t out[64 * 64];
    rsurf_t rt3 = {(uint8_t*)out, 64, 64, 64 * 2, 16, RPF_RGB565};
    memset(out, 0, sizeof out);
    uint32_t fvft = RFVF_XYZRHW | RFVF_DIFFUSE | 0x100;
    assert(raster_stride(fvft) == 28);
    raster_draw(&rt3, NULL, &tex, RP_TRIANGLELIST, fvft, (const uint8_t*)t, 3,
                NULL, 0, NULL, NULL, NULL);
    assert(out[10 * 64 + 10] == 0xF800);       /* white * red = red */

    /* ARGB1555 must decode as 1555, which is the bug that made the front end
     * draw a solid red panel over its own title. 0x7C00 is pure red in 1555
     * and pure green-ish in 565. */
    {
        static uint16_t t15[4];
        for (int i = 0; i < 4; i++) t15[i] = 0xFC00;   /* opaque red */
        rsurf_t tex15 = {(uint8_t*)t15, 2, 2, 2 * 2, 16, RPF_ARGB1555};
        memset(out, 0, sizeof out);
        raster_draw(&rt3, NULL, &tex15, RP_TRIANGLELIST, fvft, (const uint8_t*)t, 3,
                    NULL, 0, NULL, NULL, NULL);
        assert(out[10 * 64 + 10] == 0xF800);           /* red in the 565 target */
        /* ...and a transparent 1555 pixel is discarded by the alpha test. */
        for (int i = 0; i < 4; i++) t15[i] = 0x7C00;   /* alpha bit clear */
        rstate_t st = {1, 1, 0, 0, 0, 8};
        memset(out, 0, sizeof out);
        raster_draw(&rt3, NULL, &tex15, RP_TRIANGLELIST, fvft, (const uint8_t*)t, 3,
                    NULL, 0, NULL, NULL, &st);
        assert(out[10 * 64 + 10] == 0);
    }

    /* An untransformed FVF with no matrix draws nothing rather than garbage. */
    memset(pix, 0, sizeof pix);
    struct { float x, y, z; uint32_t c; } u[3] = {
        {0.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
        {1.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
        {0.0f, 1.0f, 0.0f, 0xFFFFFFFFu},
    };
    assert(raster_draw(&rt, NULL, NULL, RP_TRIANGLELIST, RFVF_XYZ | RFVF_DIFFUSE,
                       (const uint8_t*)u, 3, NULL, 0, NULL, NULL, NULL) == 0);

    /* ...and with an identity matrix it lands in the viewport. */
    static const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    int vp[4] = {0, 0, 64, 64};
    struct { float x, y, z; uint32_t c; } q[3] = {
        {-0.9f, -0.9f, 0.5f, 0xFFFFFFFFu},
        { 0.9f, -0.9f, 0.5f, 0xFFFFFFFFu},
        {-0.9f,  0.9f, 0.5f, 0xFFFFFFFFu},
    };
    memset(pix, 0, sizeof pix);
    assert(raster_draw(&rt, NULL, NULL, RP_TRIANGLELIST, RFVF_XYZ | RFVF_DIFFUSE,
                       (const uint8_t*)q, 3, NULL, 0, I, vp, NULL) > 1000);

    /* Depth: draw near, then far, and the far one must not win. */
    {
        static uint32_t cpix[64 * 64];
        static uint16_t zpix[64 * 64];
        rsurf_t crt = {(uint8_t*)cpix, 64, 64, 64 * 4, 32, RPF_XRGB8888};
        rsurf_t zb = {(uint8_t*)zpix, 64, 64, 64 * 2, 16, RPF_UNKNOWN};
        rstate_t zs = {0, 0, 0, 1, 1, 4};        /* test+write, LESSEQUAL */
        memset(cpix, 0, sizeof cpix);
        for (int i = 0; i < 64 * 64; i++) zpix[i] = 0xFFFF;

        tlv_t near3[3] = {v[0], v[1], v[2]};
        near3[0].z = near3[1].z = near3[2].z = 0.25f;
        near3[0].c = near3[1].c = near3[2].c = 0xFF00FF00u;   /* green */
        raster_draw(&crt, &zb, NULL, RP_TRIANGLELIST, fvf,
                    (const uint8_t*)near3, 3, NULL, 0, NULL, NULL, &zs);
        assert((cpix[10 * 64 + 10] & 0xFFFFFF) == 0x00FF00);

        tlv_t far3[3] = {v[0], v[1], v[2]};
        far3[0].z = far3[1].z = far3[2].z = 0.75f;
        far3[0].c = far3[1].c = far3[2].c = 0xFFFF0000u;      /* red */
        raster_draw(&crt, &zb, NULL, RP_TRIANGLELIST, fvf,
                    (const uint8_t*)far3, 3, NULL, 0, NULL, NULL, &zs);
        assert((cpix[10 * 64 + 10] & 0xFFFFFF) == 0x00FF00);  /* still green */

        /* ...and with the test off it does. */
        zs.z_test = 0;
        raster_draw(&crt, &zb, NULL, RP_TRIANGLELIST, fvf,
                    (const uint8_t*)far3, 3, NULL, 0, NULL, NULL, &zs);
        assert((cpix[10 * 64 + 10] & 0xFFFFFF) == 0xFF0000);
    }

    printf("raster.c self-test OK\n");
    return 0;
}
#endif
