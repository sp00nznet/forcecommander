/*
 * Force Commander - MSVC 6.0 std::basic_string<char> and friends.
 *
 * 58 of the 82 MSVCP60 imports are __thiscall members of basic_string. Giving
 * them correct purge counts balanced the stack; giving them stub bodies left
 * every string uninitialised, which is where the startup faulted.
 *
 * These are real implementations, and they have to match MSVC 6's *layout*, not
 * just its behaviour, because the compiler inlines the cheap accessors. The
 * game's own code contains inlined copies of c_str(), size(), length(),
 * capacity(), empty(), begin() and end(), and every one of those reads the
 * object's fields directly. Only the expensive operations became imports. So
 * the object must be:
 *
 *     +0x00  allocator<char>  empty class, but still occupies a padded slot
 *     +0x04  char*  _Ptr     always NUL-terminated, never NULL
 *     +0x08  size_t _Len     length in characters
 *     +0x0C  size_t _Res     capacity
 *                            sizeof == 16
 *
 * The allocator slot is the trap, and getting it wrong cost a debugging session.
 * MSVC 6 declares `_A allocator` as the FIRST member, and an empty class member
 * still takes a byte, padded to four -- so every field sits one slot later than
 * a reading of the standard library would suggest. The game's own copy
 * constructor says so plainly at 0x00401AD0:
 *
 *     mov  al, byte ptr [esi]          ; read the source's allocator byte
 *     mov  byte ptr [ebx], al          ; allocator -> this+0
 *     mov  dword ptr [ebx + 4], edi    ; _Ptr      -> this+4
 *     mov  dword ptr [ebx + 8], edi    ; _Len      -> this+8
 *     mov  dword ptr [ebx + 0xc], edi  ; _Res      -> this+12
 *
 * With the fields one slot early, _Grow returned having set what the caller
 * read as _Len, so 0x004EE923's `mov edi, [esp+0x14]` loaded 0 and the
 * following `rep movsd` wrote a string literal to address 0.
 *
 * and the buffer carries a reference count in the byte *before* the data --
 * which is not a guess: the import list contains
 *
 *     ?_Refcnt@...AAEAAEPBD@Z    ->  unsigned char& _Refcnt(const char*)
 *
 * a member returning a *reference to an unsigned char* given a data pointer.
 * That is MSVC 6's `((unsigned char*)_U)[-1]`.
 *
 * Copy-on-write is deliberately not implemented. Every construction and
 * assignment deep-copies and sets the refcount to 0, which MSVC 6 reads as
 * "one owner, safe to mutate in place". Sharing would be faster and is exactly
 * the sort of thing that turns a lifetime bug into a silent aliasing bug during
 * bring-up.
 *
 * ponytail: deep copy always, refcount pinned at 0. Implement sharing only if a
 * profile says string copying is hot.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "imports.h"

uint32_t crt_alloc(uint32_t n);          /* crt_shims.c */

#define NPOS 0xFFFFFFFFu

/* ---------------------------------------------------------------- object */

#define S_PTR(o)  MEM32((o) + 4)
#define S_LEN(o)  MEM32((o) + 8)
#define S_RES(o)  MEM32((o) + 12)

static uint32_t g_nullstr;               /* the shared empty buffer */

static char* host(uint32_t va) { return (char*)(uintptr_t)ADDR(va); }

/* A usable destination. _Ptr == 0 is fine -- the game zero-fills string members
 * and s_reserve() allocates on first write -- but a small NON-zero _Ptr, or a
 * length no allocation could have, means this object was never a string: either
 * `this` arrived through a wrong purge count or the storage was never built.
 * Left unchecked that became a memcpy inside msvcrt.dll with nothing to say
 * which shim or which caller was responsible. */
/* A _Ptr always points at a buf_new() allocation or at g_nullstr, so it lies in
 * the simulated stack (0x00200000..0x00300000) or heap (0x10000000 + 128 MB).
 * The upper bound matters as much as the lower one: the first version of this
 * only rejected small values and let a _Ptr of 0x9C464C95 straight through. */
#define S_LOW  0x00200000u
#define S_HIGH 0x18000000u
#define S_MAXLEN 0x08000000u

static int s_ptr_ok(uint32_t p) { return p >= S_LOW && p < S_HIGH; }

static int s_bad(uint32_t o) {
    if (o < S_LOW || o >= S_HIGH) return 1;
    uint32_t p = S_PTR(o);
    return (p && !s_ptr_ok(p)) || S_LEN(o) > S_MAXLEN;
}


/*
 * Read the data pointer of ANOTHER string object, checked.
 *
 * Every shim that copies out of a second string went straight through
 * s_src(x, NULL), so an object that was never constructed -- or one reached
 * through a wrong purge count -- turned into a memcpy from whatever those four
 * bytes happened to hold. That surfaced as a fault inside msvcrt.dll with no
 * indication of which shim or which string was at fault. Reporting it names the
 * call site instead, and treating the string as empty lets the run continue to
 * the next real problem.
 *
 * The valid range is the simulated stack and heap: a _Ptr always points at a
 * buf_new() allocation or at g_nullstr, never into the image.
 */
static const char* s_src(uint32_t o, uint32_t* len) {
    if (s_bad(o)) {
        fprintf(stderr, "[stl] read of a non-string 0x%08X (_Ptr=0x%08X)"
                        " from 0x%08X\n",
                o, o >= S_LOW ? S_PTR(o) : 0, g_cur_func);
        static int seen = 0;
        if (seen++ == 0) recomp_dump_trace("non-string read");
        if (seen > 20) { if (len) *len = 0; return ""; }
        if (len) *len = 0;
        return "";
    }
    uint32_t p = S_PTR(o);
    if (len) *len = p ? S_LEN(o) : 0;
    return p ? host(p) : "";
}


/* Allocate a buffer for `cap` characters: one refcount byte, the data, a NUL. */
static int g_trace_stl = 0;

static uint32_t buf_new(uint32_t cap) {
    uint32_t p = crt_alloc(cap + 2);
    if (!p) {
        fprintf(stderr, "[stl] buf_new(%u) FAILED -- heap exhausted,"
                        " from 0x%08X\n", cap, g_cur_func);
        abort();
    }
    MEM8(p) = 0;                          /* refcount, at data[-1] */
    uint32_t data = p + 1;
    MEM8(data) = 0;
    return data;
}

static void s_set(uint32_t o, const char* src, uint32_t n, uint32_t cap) {
    if (cap < n) cap = n;
    uint32_t data = buf_new(cap);
    if (!data) { S_PTR(o) = g_nullstr; S_LEN(o) = 0; S_RES(o) = 0; return; }
    if (n && src) memcpy(host(data), src, n);
    MEM8(data + n) = 0;
    S_PTR(o) = data;
    S_LEN(o) = n;
    S_RES(o) = cap;
}

/* Make room for `need` characters, preserving the current contents. */
static void s_reserve(uint32_t o, uint32_t need) {
    /* The single path every growing write takes, so the check belongs here
     * rather than in each of the twenty-odd callers. */
    if (s_bad(o)) {
        fprintf(stderr, "[stl] reserve(%u) on a non-string 0x%08X"
                        " (_Ptr=0x%08X) from 0x%08X\n",
                need, o, o >= S_LOW && o < S_HIGH ? S_PTR(o) : 0, g_cur_func);
        return;
    }
    if (S_RES(o) >= need && S_PTR(o) != g_nullstr) return;
    uint32_t cap = S_RES(o) ? S_RES(o) : 15;
    while (cap < need) cap = cap * 2 + 1;
    uint32_t len = S_LEN(o);
    uint32_t data = buf_new(cap);
    if (!data) return;
    if (len) memcpy(host(data), s_src(o, NULL), len);
    MEM8(data + len) = 0;
    S_PTR(o) = data;
    S_RES(o) = cap;
}

static void s_empty(uint32_t o) {
    S_PTR(o) = g_nullstr;
    S_LEN(o) = 0;
    S_RES(o) = 0;
}

static int s_dst_ok(uint32_t o, const char* who) {
    if (!s_bad(o)) return 1;
    fprintf(stderr, "[stl] %s on a non-string this=0x%08X (_Ptr=0x%08X"
                    " _Len=%u) from 0x%08X\n",
            who, o, o >= S_LOW ? S_PTR(o) : 0, o >= S_LOW ? S_LEN(o) : 0,
            g_cur_func);
    return 0;
}

static void s_assign(uint32_t o, const char* src, uint32_t n) {
    if (!s_dst_ok(o, "assign")) return;
    if (!n) { s_empty(o); return; }
    s_reserve(o, n);
    if (S_PTR(o) == g_nullstr) return;
    memcpy(s_src(o, NULL), src, n);
    MEM8(S_PTR(o) + n) = 0;
    S_LEN(o) = n;
}

static void s_append(uint32_t o, const char* src, uint32_t n) {
    if (!n) return;
    if (!s_dst_ok(o, "append")) return;
    uint32_t len = S_LEN(o);
    s_reserve(o, len + n);
    if (S_PTR(o) == g_nullstr) return;
    memcpy(s_src(o, NULL) + len, src, n);
    MEM8(S_PTR(o) + len + n) = 0;
    S_LEN(o) = len + n;
}

/* `this` arrives in ecx for __thiscall; it is not a popped argument. */
#define THIS  (g_ecx)

/* ------------------------------------------------------- constructors */

/* basic_string(const allocator&) -- the default constructor in MSVC 6 */
static void s_ctor_alloc(void) { s_empty(THIS); RET(THIS); STDRET(1); }

/* basic_string(const char*, const allocator&) */
static void s_ctor_cstr(void) {
    uint32_t src = ARG(0);
    const char* p = src ? host(src) : "";
    s_set(THIS, p, (uint32_t)strlen(p), 0);
    RET(THIS); STDRET(2);
}

/* basic_string(const basic_string&) */
static void s_ctor_copy(void) {
    uint32_t rhs = ARG(0);
    s_set(THIS, rhs ? s_src(rhs, NULL) : "", rhs ? S_LEN(rhs) : 0, 0);
    RET(THIS); STDRET(1);
}

/* ~basic_string(), and the default-ctor closure ??_F */
static void s_dtor(void)   { s_empty(THIS); RET(THIS); STDRET(0); }
static void s_ctor_f(void) { s_empty(THIS); RET(THIS); STDRET(0); }

/* ------------------------------------------------------------ assign */

static void s_op_assign_cstr(void) {
    const char* p = ARG(0) ? host(ARG(0)) : "";
    s_assign(THIS, p, (uint32_t)strlen(p));
    RET(THIS); STDRET(1);
}
static void s_assign_cstr(void) {
    const char* p = ARG(0) ? host(ARG(0)) : "";
    s_assign(THIS, p, (uint32_t)strlen(p));
    RET(THIS); STDRET(1);
}
static void s_assign_cstr_n(void) {
    const char* p = ARG(0) ? host(ARG(0)) : "";
    s_assign(THIS, p, ARG(1));
    RET(THIS); STDRET(2);
}
static void s_assign_str_sub(void) {
    uint32_t rhs = ARG(0), pos = ARG(1), n = ARG(2);
    uint32_t rl = rhs ? S_LEN(rhs) : 0;
    if (pos > rl) pos = rl;
    if (n > rl - pos) n = rl - pos;
    s_assign(THIS, s_src(rhs, NULL) + pos, n);
    RET(THIS); STDRET(3);
}

/* ------------------------------------------------------------ append */

static void s_op_append_str(void) {
    uint32_t rhs = ARG(0);
    s_append(THIS, rhs ? s_src(rhs, NULL) : "", rhs ? S_LEN(rhs) : 0);
    RET(THIS); STDRET(1);
}
static void s_op_append_cstr(void) {
    const char* p = ARG(0) ? host(ARG(0)) : "";
    s_append(THIS, p, (uint32_t)strlen(p));
    RET(THIS); STDRET(1);
}
static void s_append_str_sub(void) {
    uint32_t rhs = ARG(0), pos = ARG(1), n = ARG(2);
    uint32_t rl = rhs ? S_LEN(rhs) : 0;
    if (pos > rl) pos = rl;
    if (n > rl - pos) n = rl - pos;
    s_append(THIS, s_src(rhs, NULL) + pos, n);
    RET(THIS); STDRET(3);
}
static void s_append_n_ch(void) {
    uint32_t n = ARG(0);
    char c = (char)ARG(1);
    uint32_t len = S_LEN(THIS);
    s_reserve(THIS, len + n);
    if (S_PTR(THIS) != g_nullstr) {
        memset(s_src(THIS, NULL) + len, c, n);
        MEM8(S_PTR(THIS) + len + n) = 0;
        S_LEN(THIS) = len + n;
    }
    RET(THIS); STDRET(2);
}
static void s_append_cstr_n(void) {
    const char* p = ARG(0) ? host(ARG(0)) : "";
    s_append(THIS, p, ARG(1));
    RET(THIS); STDRET(2);
}

/* ------------------------------------------------------------ queries */

static void s_index(void) {        /* operator[](size_t) const -> const char& */
    uint32_t i = ARG(0);
    RET(S_PTR(THIS) + (i <= S_LEN(THIS) ? i : S_LEN(THIS)));
    STDRET(1);
}
static void s_max_size(void) { RET(0x7FFFFFFEu); STDRET(0); }

static uint32_t find_fwd(uint32_t o, const char* pat, uint32_t pos, uint32_t n,
                         int want_in_set, int use_set) {
    uint32_t len = S_LEN(o);
    const char* s = s_src(o, NULL);
    if (!use_set) {
        if (n > len || pos > len - n) return NPOS;
        for (uint32_t i = pos; i + n <= len; i++)
            if (!memcmp(s + i, pat, n)) return i;
        return NPOS;
    }
    for (uint32_t i = pos; i < len; i++) {
        int in = memchr(pat, s[i], n) != NULL;
        if (in == want_in_set) return i;
    }
    return NPOS;
}

static uint32_t find_rev(uint32_t o, const char* pat, uint32_t pos, uint32_t n,
                         int want_in_set) {
    uint32_t len = S_LEN(o);
    if (!len) return NPOS;
    const char* s = s_src(o, NULL);
    uint32_t i = (pos >= len) ? len - 1 : pos;
    for (;;) {
        int in = memchr(pat, s[i], n) != NULL;
        if (in == want_in_set) return i;
        if (i == 0) break;
        i--;
    }
    return NPOS;
}

/* All five take (const char*, size_t pos, size_t n). */
static void s_find(void) {
    RET(find_fwd(THIS, host(ARG(0)), ARG(1), ARG(2), 0, 0)); STDRET(3);
}
static void s_find_first_of(void) {
    RET(find_fwd(THIS, host(ARG(0)), ARG(1), ARG(2), 1, 1)); STDRET(3);
}
static void s_find_first_not_of(void) {
    RET(find_fwd(THIS, host(ARG(0)), ARG(1), ARG(2), 0, 1)); STDRET(3);
}
static void s_find_last_of(void) {
    RET(find_rev(THIS, host(ARG(0)), ARG(1), ARG(2), 1)); STDRET(3);
}
static void s_find_last_not_of(void) {
    RET(find_rev(THIS, host(ARG(0)), ARG(1), ARG(2), 0)); STDRET(3);
}

static void s_copy_out(void) {     /* copy(char* dst, size_t n, size_t pos) */
    uint32_t dst = ARG(0), n = ARG(1), pos = ARG(2), len = S_LEN(THIS);
    if (pos > len) pos = len;
    if (n > len - pos) n = len - pos;
    if (n) memcpy(host(dst), s_src(THIS, NULL) + pos, n);
    RET(n); STDRET(3);
}

/* -------------------------------------------------------- modifications */

static void s_erase(void) {        /* erase(size_t pos, size_t n) */
    uint32_t pos = ARG(0), n = ARG(1), len = S_LEN(THIS);
    if (pos > len) pos = len;
    if (n > len - pos) n = len - pos;
    char* s = s_src(THIS, NULL);
    memmove(s + pos, s + pos + n, len - pos - n);
    S_LEN(THIS) = len - n;
    MEM8(S_PTR(THIS) + S_LEN(THIS)) = 0;
    RET(THIS); STDRET(2);
}

static void s_resize(void) {       /* resize(size_t n) -- pads with '\0' */
    uint32_t n = ARG(0), len = S_LEN(THIS);
    if (n > len) {
        s_reserve(THIS, n);
        if (S_PTR(THIS) != g_nullstr)
            memset(s_src(THIS, NULL) + len, 0, n - len);
    }
    if (S_PTR(THIS) != g_nullstr) {
        S_LEN(THIS) = n;
        MEM8(S_PTR(THIS) + n) = 0;
    }
    RET(THIS); STDRET(1);
}

static void s_replace(void) {  /* replace(pos,n, const string&, pos2,n2) */
    uint32_t pos = ARG(0), n = ARG(1), rhs = ARG(2), pos2 = ARG(3), n2 = ARG(4);
    uint32_t len = S_LEN(THIS), rl = rhs ? S_LEN(rhs) : 0;
    if (pos > len) pos = len;
    if (n > len - pos) n = len - pos;
    if (pos2 > rl) pos2 = rl;
    if (n2 > rl - pos2) n2 = rl - pos2;

    uint32_t out = len - n + n2;
    uint32_t tmp = buf_new(out);
    if (!tmp) { RET(THIS); STDRET(5); return; }
    char* d = host(tmp);
    memcpy(d, s_src(THIS, NULL), pos);
    if (n2) memcpy(d + pos, s_src(rhs, NULL) + pos2, n2);
    memcpy(d + pos + n2, s_src(THIS, NULL) + pos + n, len - pos - n);
    d[out] = 0;
    S_PTR(THIS) = tmp; S_LEN(THIS) = out; S_RES(THIS) = out;
    RET(THIS); STDRET(5);
}

/*
 * substr(pos, n) const. Returns a string BY VALUE, so MSVC passes a hidden
 * pointer to caller-allocated storage as the first stack argument and the
 * function returns that same pointer in eax. Three slots: the hidden pointer
 * plus the two size_t.
 */
static void s_substr(void) {
    uint32_t out = ARG(0), pos = ARG(1), n = ARG(2), len = S_LEN(THIS);
    if (pos > len) pos = len;
    if (n > len - pos) n = len - pos;
    s_set(out, s_src(THIS, NULL) + pos, n, 0);
    RET(out); STDRET(3);
}

/* ----------------------------------------------------------- internals */

/*
 * The private helpers. The game's inlined code calls these directly, so they
 * have to exist, but with copy-on-write turned off most of them are no-ops:
 * _Freeze and _Split exist to break sharing, and nothing is ever shared.
 */
static void s_Tidy(void)   { if (!S_PTR(THIS)) s_empty(THIS); RET(THIS); STDRET(1); }
static void s_Freeze(void) { RET(THIS); STDRET(0); }
static void s_Split(void)  { RET(THIS); STDRET(0); }
static void s_Refcnt(void) { RET(ARG(0) ? ARG(0) - 1 : g_nullstr); STDRET(1); }
static void s_Eos(void) {
    uint32_t n = ARG(0);
    if (S_PTR(THIS) != g_nullstr) { S_LEN(THIS) = n; MEM8(S_PTR(THIS) + n) = 0; }
    RET(THIS); STDRET(1);
}
static void s_Copy(void)   { s_reserve(THIS, ARG(0)); RET(THIS); STDRET(1); }
static void s_Grow(void) {
    uint32_t n = ARG(0);
    if (g_trace_stl)
        fprintf(stderr, "[stl] _Grow(this=0x%08X, n=%u, trim=%u) before:"
                        " _Ptr=0x%08X _Len=%u _Res=%u from 0x%08X\n",
                THIS, n, ARG(1), S_PTR(THIS), S_LEN(THIS), S_RES(THIS),
                g_cur_func);
    if (!n) { s_empty(THIS); RET(0); STDRET(2); return; }
    s_reserve(THIS, n);
    RET(1); STDRET(2);
}

/* ------------------------------------------- free functions (all cdecl) */

static int cmp_str_str(uint32_t a, uint32_t b) {
    uint32_t la = S_LEN(a), lb = S_LEN(b);
    uint32_t n = la < lb ? la : lb;
    int r = n ? memcmp(s_src(a, NULL), s_src(b, NULL), n) : 0;
    if (r) return r;
    return la == lb ? 0 : (la < lb ? -1 : 1);
}
static int cmp_str_cstr(uint32_t a, uint32_t b) {
    return strcmp(s_src(a, NULL), b ? host(b) : "");
}

static void f_eq_ss(void)  { RET(cmp_str_str(ARG(0), ARG(1)) == 0); CDECLRET(); }
static void f_eq_sc(void)  { RET(cmp_str_cstr(ARG(0), ARG(1)) == 0); CDECLRET(); }
static void f_eq_cs(void)  { RET(cmp_str_cstr(ARG(1), ARG(0)) == 0); CDECLRET(); }
static void f_ne_ss(void)  { RET(cmp_str_str(ARG(0), ARG(1)) != 0); CDECLRET(); }
static void f_ne_sc(void)  { RET(cmp_str_cstr(ARG(0), ARG(1)) != 0); CDECLRET(); }
static void f_lt_ss(void)  { RET(cmp_str_str(ARG(0), ARG(1)) < 0); CDECLRET(); }
static void f_gt_ss(void)  { RET(cmp_str_str(ARG(0), ARG(1)) > 0); CDECLRET(); }

/* operator+ returns by value: hidden out pointer is the first argument. */
static void f_add_ss(void) {
    uint32_t o = ARG(0), a = ARG(1), b = ARG(2);
    s_set(o, s_src(a, NULL), S_LEN(a), S_LEN(a) + S_LEN(b));
    uint32_t bl; const char* bp = s_src(b, &bl);
    s_append(o, bp, bl);
    RET(o); CDECLRET();
}
static void f_add_sc(void) {
    uint32_t o = ARG(0), a = ARG(1);
    const char* b = ARG(2) ? host(ARG(2)) : "";
    uint32_t al; const char* ap = s_src(a, &al);
    s_set(o, ap, al, 0);
    s_append(o, b, (uint32_t)strlen(b));
    RET(o); CDECLRET();
}
static void f_add_cs(void) {
    uint32_t o = ARG(0);
    const char* a = ARG(1) ? host(ARG(1)) : "";
    uint32_t b = ARG(2);
    s_set(o, a, (uint32_t)strlen(a), 0);
    uint32_t bl; const char* bp = s_src(b, &bl);
    s_append(o, bp, bl);
    RET(o); CDECLRET();
}
static void f_add_s_ch(void) {
    uint32_t o = ARG(0), a = ARG(1);
    char c = (char)ARG(2);
    s_set(o, s_src(a, NULL), S_LEN(a), S_LEN(a) + 1);
    s_append(o, &c, 1);
    RET(o); CDECLRET();
}

static void f_ostream_put(void) { RET(ARG(0)); CDECLRET(); }

/* char_traits<char> statics */
static void ct_length(void) { RET((uint32_t)strlen(host(ARG(0)))); CDECLRET(); }
static void ct_copy(void)   { memcpy(host(ARG(0)), host(ARG(1)), ARG(2)); RET(ARG(0)); CDECLRET(); }
static void ct_assign(void) { MEM8(ARG(0)) = MEM8(ARG(1)); RET(ARG(0)); CDECLRET(); }

/* The throwers. Reaching either is a bug in the caller, not something to
 * swallow: length_error and out_of_range both mean the game asked for
 * something impossible, and continuing hides where. */
static void f_Xlen(void) {
    fprintf(stderr, "[stl] std::_Xlen (length_error) from 0x%08X\n", g_cur_func);
    CDECLRET();
}
static void f_Xran(void) {
    fprintf(stderr, "[stl] std::_Xran (out_of_range) from 0x%08X\n", g_cur_func);
    CDECLRET();
}

/* iostream/locale: still stubs. Nothing on the startup path uses an fstream
 * for real, and a half-implemented streambuf is worse than an obvious no-op. */
static void nop0(void) { RET(THIS); STDRET(0); }
static void nop1(void) { RET(THIS); STDRET(1); }
static void nop2(void) { RET(THIS); STDRET(2); }

/* ------------------------------------------------------------- registry */

#define STR "?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
#define IOS "?$basic_ios@DU?$char_traits@D@std@@@std@@"
#define FBUF "?$basic_filebuf@DU?$char_traits@D@std@@@std@@"
#define SBUF "?$basic_streambuf@DU?$char_traits@D@std@@@std@@"
#define FSTR "?$basic_fstream@DU?$char_traits@D@std@@@std@@"
#define IOST "?$basic_iostream@DU?$char_traits@D@std@@@std@@"
/* In a FREE function the enclosing std:: is a backref, so basic_string ends
 * at `@2@` and is followed by `@0@` -- not the `@2@@std@@` spelling a member
 * uses. Same type, different encoding, and the table silently missed all 13
 * operators until the names were diffed against the import list. */
#define STRF "?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@"
#define OSTR "?$basic_ostream@DU?$char_traits@D@std@@"
#define P "MSVCP60.dll!"

const struct { const char* name; import_fn_t fn; } g_stl_shims[] = {
    { P "??0" STR "QAE@ABV?$allocator@D@1@@Z",      s_ctor_alloc },
    { P "??0" STR "QAE@PBDABV?$allocator@D@1@@Z",   s_ctor_cstr },
    { P "??0" STR "QAE@ABV01@@Z",                   s_ctor_copy },
    { P "??1" STR "QAE@XZ",                         s_dtor },
    { P "??_F" STR "QAEXXZ",                        s_ctor_f },
    { P "??4" STR "QAEAAV01@PBD@Z",                 s_op_assign_cstr },
    { P "??A" STR "QBEABDI@Z",                      s_index },
    { P "??Y" STR "QAEAAV01@ABV01@@Z",              s_op_append_str },
    { P "??Y" STR "QAEAAV01@PBD@Z",                 s_op_append_cstr },
    { P "?assign@" STR "QAEAAV12@PBD@Z",            s_assign_cstr },
    { P "?assign@" STR "QAEAAV12@PBDI@Z",           s_assign_cstr_n },
    { P "?assign@" STR "QAEAAV12@ABV12@II@Z",       s_assign_str_sub },
    { P "?append@" STR "QAEAAV12@ABV12@II@Z",       s_append_str_sub },
    { P "?append@" STR "QAEAAV12@ID@Z",             s_append_n_ch },
    { P "?append@" STR "QAEAAV12@PBDI@Z",           s_append_cstr_n },
    { P "?max_size@" STR "QBEIXZ",                  s_max_size },
    { P "?find@" STR "QBEIPBDII@Z",                 s_find },
    { P "?find_first_of@" STR "QBEIPBDII@Z",        s_find_first_of },
    { P "?find_first_not_of@" STR "QBEIPBDII@Z",    s_find_first_not_of },
    { P "?find_last_of@" STR "QBEIPBDII@Z",         s_find_last_of },
    { P "?find_last_not_of@" STR "QBEIPBDII@Z",     s_find_last_not_of },
    { P "?copy@" STR "QBEIPADII@Z",                 s_copy_out },
    { P "?erase@" STR "QAEAAV12@II@Z",              s_erase },
    { P "?resize@" STR "QAEXI@Z",                   s_resize },
    { P "?replace@" STR "QAEAAV12@IIABV12@II@Z",    s_replace },
    { P "?substr@" STR "QBE?AV12@II@Z",             s_substr },
    { P "?_Tidy@" STR "AAEX_N@Z",                   s_Tidy },
    { P "?_Freeze@" STR "AAEXXZ",                   s_Freeze },
    { P "?_Split@" STR "AAEXXZ",                    s_Split },
    { P "?_Refcnt@" STR "AAEAAEPBD@Z",              s_Refcnt },
    { P "?_Eos@" STR "AAEXI@Z",                     s_Eos },
    { P "?_Copy@" STR "AAEXI@Z",                    s_Copy },
    { P "?_Grow@" STR "AAE_NI_N@Z",                 s_Grow },

    { P "??8std@@YA_NABV" STRF "@0@0@Z",            f_eq_ss },
    { P "??8std@@YA_NABV" STRF "@0@PBD@Z",          f_eq_sc },
    { P "??8std@@YA_NPBDABV" STRF "@0@@Z",          f_eq_cs },
    { P "??9std@@YA_NABV" STRF "@0@0@Z",            f_ne_ss },
    { P "??9std@@YA_NABV" STRF "@0@PBD@Z",          f_ne_sc },
    { P "??Mstd@@YA_NABV" STRF "@0@0@Z",            f_lt_ss },
    { P "??Ostd@@YA_NABV" STRF "@0@0@Z",            f_gt_ss },
    { P "??Hstd@@YA?AV" STRF "@0@ABV10@0@Z",        f_add_ss },
    { P "??Hstd@@YA?AV" STRF "@0@ABV10@PBD@Z",      f_add_sc },
    { P "??Hstd@@YA?AV" STRF "@0@PBDABV10@@Z",      f_add_cs },
    { P "??Hstd@@YA?AV" STRF "@0@ABV10@D@Z",        f_add_s_ch },

    /* operator<<(ostream&, ...) -- iostream is not implemented; return the
     * stream so a chained << does not fault, and drop the output. */
    { P "??6std@@YAAAV" OSTR "@0@AAV10@ABV" STRF "@0@@Z", f_ostream_put },
    { P "??6std@@YAAAV" OSTR "@0@AAV10@PBD@Z",      f_ostream_put },
    { P "?length@?$char_traits@D@std@@SAIPBD@Z",    ct_length },
    { P "?copy@?$char_traits@D@std@@SAPADPADPBDI@Z", ct_copy },
    { P "?assign@?$char_traits@D@std@@SAXAADABD@Z", ct_assign },
    { P "?_Xlen@std@@YAXXZ",                        f_Xlen },
    { P "?_Xran@std@@YAXXZ",                        f_Xran },

    /* iostream / locale / lock: no-ops with correct purges. */
    { P "??0Init@ios_base@std@@QAE@XZ",             nop0 },
    { P "??1Init@ios_base@std@@QAE@XZ",             nop0 },
    { P "??0_Winit@std@@QAE@XZ",                    nop0 },
    { P "??1_Winit@std@@QAE@XZ",                    nop0 },
    { P "??0_Lockit@std@@QAE@XZ",                   nop0 },
    { P "??1_Lockit@std@@QAE@XZ",                   nop0 },
    { P "??0ios_base@std@@IAE@XZ",                  nop0 },
    { P "??1ios_base@std@@UAE@XZ",                  nop0 },
    { P "??1locale@std@@QAE@XZ",                    nop0 },
    { P "??0" IOS "IAE@XZ",                         nop0 },
    { P "??1" IOS "UAE@XZ",                         nop0 },
    { P "??1" SBUF "UAE@XZ",                        nop0 },
    { P "??1" FBUF "UAE@XZ",                        nop0 },
    { P "??1" FSTR "UAE@XZ",                        nop0 },
    { P "??1" IOST "UAE@XZ",                        nop0 },
    { P "??_D" FSTR "QAEXXZ",                       nop0 },
    { P "??0" FBUF "QAE@PAU_iobuf@@@Z",             nop1 },
    { P "??0" IOST "QAE@PAV" SBUF "@1@@Z",          nop1 },
    { P "?close@" FBUF "QAEPAV12@XZ",               nop0 },
    { P "?open@" FBUF "QAEPAV12@PBDH@Z",            nop2 },
    { P "?clear@" IOS "QAEXH_N@Z",                  nop2 },
    { P "?clear@ios_base@std@@QAEXH_N@Z",           nop2 },
    { P "?setstate@" IOS "QAEXH_N@Z",               nop2 },
};
const unsigned g_stl_shim_count = sizeof(g_stl_shims) / sizeof(g_stl_shims[0]);

/*
 * Data imports. The slot holds the *address* of the value, so each needs real
 * storage. npos is the one that matters most: with the slot self-patched to its
 * own address, `npos` read as 0x007C3248, and that value was in eax, edx and
 * esi at the fault that stopped the previous run.
 */
void stl_init_data_imports(void) {
    g_trace_stl = getenv("FOCOM_TRACE_STL") != NULL;
    uint32_t npos = crt_alloc(4);
    MEM32(npos) = NPOS;

    g_nullstr = crt_alloc(4);          /* the shared empty buffer */
    MEM32(g_nullstr) = 0;

    uint32_t vt = crt_alloc(64);       /* a shared all-zero stand-in vtable */
    memset((void*)(uintptr_t)ADDR(vt), 0, 64);

    for (unsigned i = 0; i < g_import_count; i++) {
        const char* n = g_imports[i].name;
        if (!g_imports[i].conv || strcmp(g_imports[i].conv, "data")) continue;
        if (strstr(n, "?npos@"))            MEM32(g_imports[i].iat_va) = npos;
        else if (strstr(n, "_Nullstr"))     MEM32(g_imports[i].iat_va) = g_nullstr;
        else                                MEM32(g_imports[i].iat_va) = vt;
    }
    printf("  npos=0x%08X nullstr=0x%08X\n", npos, g_nullstr);
}
