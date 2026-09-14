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
uint32_t  g_icall_from[ICALL_TRACE_SIZE] = {0};
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
void ddraw_set_dumpframe(const char* path);   /* ddraw_shims.c */
void ddraw_set_uimap(void);                   /* ddraw_shims.c */
void ddraw_set_drawprobe(int x, int y);       /* ddraw_shims.c */

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

/* Is this plausibly a readable target address?
 *
 * A script probe walks structures the script builds, and not every field it
 * reaches is a pointer -- [line+8] is an argument block for one thunk shape and
 * an immediate for another. ">= 0x200000" was the guard and it is not enough:
 * a string of text passes it (0x696E4900 is a fragment of "Init") and the probe faulted
 * inside the interpreter, which then reads as a game bug that is not there.
 * The mapped world is the image at 0x00400000 and the heap through
 * 0x50000000. */
#define T_OK(a) ((a) >= 0x00200000u && (a) < 0x50000000u)

#define GET_LIBRARY  0x0052C300u
#define VIS_RUN_LINE 0x00512170u
#define VIS_STEP      0x005127E0u
static int g_scripttrace;
/*
 * --nolib: the same GetLibrary report as --scripttrace, on its own.
 *
 * A script line that names a subsystem the exe never registered gets NULL
 * back and does nothing, silently -- which is the shape of a front-end item
 * that runs its whole Case and changes nothing. That report is worth having
 * without the per-line flood, which is hundreds of megabytes a run.
 */
static int g_nolibtrace;

/*
 * --scripttracefrom MS [--scripttracefor MS]: a window, not a whole run.
 *
 * The script trace is one line per executed script line per frame, which is
 * hundreds of megabytes a minute -- unreadable, and slow enough to change
 * what the game does. What a question about a click needs is the twenty
 * seconds around the click, so a host thread turns the trace on at one time
 * and off at another.
 */
static DWORD g_st_from, g_st_for;

/*
 * --varpoke SLOT VALUE: pin one script variable.
 *
 * A script variable lives in the block's own table and its address is
 * different every run, so --poison and --poke cannot reach it: the address is
 * only known from inside the trace that computed it. The slot NUMBER is
 * stable, because it is compiled into the bytecode.
 *
 * This is the "force the gate open" experiment. The front end's page
 * transition spins in a While that never exits; writing the slot its
 * condition reads says whether that loop is the gate or a symptom.
 *
 * ponytail: written on every read of that slot, with no restore, for the
 * whole run. It is a diagnostic, not a fix.
 */
/*
 * --nodedump LINE: the raw argument block of one script line.
 *
 * GamePPGlobalSysWhile::Execute is sub_005D2D30, and it says exactly where a
 * control-flow line keeps its parts:
 *
 *     ecx = [args];  ecx = (ecx >> 16) & 0x3F;  ecx -= 3     ; operand count
 *     evaluate(ctx, args + 0xC, count) -> result
 *     if (!result) jump to [args + 8]                        ; the target
 *
 * So the CONDITION is a list of operand nodes starting at args+0xC, and the
 * `op` this trace prints for a control-flow line -- [args+8] -- is its jump
 * target and not a variable slot at all. Dumping the block is how the operand
 * layout gets read rather than guessed.
 */
/*
 * --nocond LINES LINE: make one control-flow line's condition false.
 *
 * GamePPGlobalSysWhile::Execute (sub_005D2D30) is:
 *
 *     result = 0                                 ; on its own stack
 *     n = (([args] >> 16) & 0x3F) - 3            ; operand count
 *     evaluate(ctx, args + 0xC, n, &result)
 *     if (!decide(result)) jump to [args + 8]
 *
 * and the result is initialised to ZERO before the call. So an operand count
 * of nothing leaves it zero and the condition is false -- which means the
 * condition of any While, If or Wait If can be forced false by writing 3 into
 * that six-bit field, without touching a single operand or knowing the
 * operand grammar.
 *
 * Which is what this is for. The front end's "For CD" thread spins in a While
 * whose first operand is the variable "Min CD Number"; the disc IS present
 * and the check fails for reasons that are ours, so skipping the wait is how
 * to find out what is behind it.
 *
 * ponytail: a diagnostic, and it says so. One line, patched once, no restore,
 * identified by its block's line count and its own line number -- which is
 * how a script line is named when its address is different every run.
 */
static int g_nocond_lines = -1, g_nocond_line = -1, g_nocond_done;
static int g_nocond_slot = -1;   /* and operand 0 must be this variable */

static int      g_nodedump = -1;

/*
 * --varxref SLOT MS: every script line that references one variable slot.
 *
 * The trace says which lines RAN. When the answer is "a condition reads slot
 * 83 and nothing ever assigns it", the useful question is the other one: which
 * lines mention slot 83 at all, and which of those never ran?
 *
 * The script is all in the heap and its shapes are known, so one pass finds
 * them. A compiled line record is 32 bytes whose first dword is one of the
 * five per-line thunks in sub_00512060..sub_005121E0 and whose +8 is its
 * argument block; the argument block's operand count is
 * (([args] >> 16) & 0x3F) - 3 and its operands are 12 bytes each from
 * args+0xC, each `[word][0][slot]` with a kind of 1 in the word's top nibble
 * for a variable.
 *
 * Line records of one block are contiguous, so walking back while the first
 * dword is still a thunk finds the block's line array, and a second pass finds
 * the block whose [+0x18] is that array.
 *
 * ponytail: no lock. It reads target memory while the target runs, so a
 * report can in principle be torn; it is a diagnostic, and taking the machine
 * lock from a thread with no saved state is the more dangerous option.
 */
#define VX_BASES 64

static uint32_t g_vx_slot;
static DWORD    g_vx_ms;
static int      g_vx_on;

uint32_t crt_heap_top(void);            /* crt_shims.c */

static int vx_is_thunk(uint32_t v) {
    return v >= 0x00512000u && v < 0x00512300u;
}

/*
 * --threadlist MS: every script thread, by name, with its state.
 *
 * --varxref found the shape by accident and it is the most useful shape in
 * the heap. A thread record is
 *
 *     +00 name (char*)   +0C container   +10 code block
 *     +14 0   +18 0      +1C index       +20 running context, or -1
 *
 * and a code block is recognisable by its GamePPVisCodeBlock vtable at +0 with
 * its line array at +0x18 and line count at +0x1C. So one pass over the heap
 * names every thread the front end has, says how many lines it is, and says
 * whether it is running -- which is the map of the whole front end, and the
 * way to see which page's thread should have started and did not.
 */
#define VIS_CODEBLOCK_VTBL 0x007C5990u

/*
 * T_OK is not enough for a heap SCAN.
 *
 * It answers "could this be a target pointer", which is the right question
 * for a probe following a chain the target built. A scan follows dwords that
 * are not pointers at all, and the heap is 1 GB RESERVED with pages committed
 * on touch -- so a plausible-looking dword into the uncommitted part reads as
 * a host segfault. Dereference only what is known to be mapped: the image,
 * the stack, or the heap up to the bump pointer.
 */
/*
 * ...and a range check is still not enough.
 *
 * The heap is 1 GB RESERVED with pages committed as the bump allocator
 * touches them, so "below the bump pointer" does not mean "mapped": a large
 * allocation's interior can be untouched, and a scan reading it faults. Ask
 * the operating system, and cache the answer, because a scan asks about the
 * same page thousands of times in a row.
 *
 * ponytail: one-entry cache, no invalidation. The target only ever commits
 * more, never less, so a stale "committed" cannot become wrong -- and a stale
 * "not committed" costs one skipped dword on a page that has just appeared.
 */
static int vx_committed(uint32_t a, uint32_t n) {
    static uintptr_t lo, hi;
    uintptr_t p0 = (uintptr_t)ADDR(a), p1 = p0 + n;
    if (p0 >= lo && p1 <= hi) return 1;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)p0, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    lo = (uintptr_t)mbi.BaseAddress;
    hi = lo + mbi.RegionSize;
    return p1 <= hi;
}

static int vx_mapped(uint32_t a, uint32_t n, uint32_t heap_top) {
    /* The overflow check comes FIRST and covers every branch. Without it
     * 0xFFFFFFFF + 0x20 wraps to 0x1F, which is <= the end of the image, so a
     * dword of 0xFFFFFFFF read as a pointer passed the image test and the
     * scan faulted reading target address 0xFFFFFFFF. */
    if (n == 0 || a + n < a) return 0;
    if (a >= FOCOM_HEAP_BASE && a + n <= heap_top) return 1;
    if (a >= FOCOM_IMAGE_BASE && a + n <= FOCOM_IMAGE_BASE + 0x00800000u)
        return 1;
    if (a >= FOCOM_STACK_BASE && a + n <= FOCOM_STACK_TOP) return 1;
    return 0;
}

/* The range test and the commit test together, which is what a scan needs. */
static int vx_readable(uint32_t a, uint32_t n, uint32_t heap_top) {
    return vx_mapped(a, n, heap_top) && vx_committed(a, n);
}

/*
 * --windows MS: every top-level window this process owns, once.
 *
 * "There are two windows" is a report about what is on screen, and the only
 * way to answer it is to ask Windows rather than to reason about which
 * ShowWindow calls were made. The game makes two of its own -- a
 * CreateDialogParamA(101) loading panel and its real CreateWindowExA one --
 * and the host makes a third, so which are visible and how big they are is
 * the whole question.
 */
static DWORD g_wl_ms;
static int   g_wl_on;
static HWND  g_hwnd;                 /* the host's own window, made below */

static BOOL CALLBACK window_row(HWND h, LPARAM unused) {
    (void)unused;
    if (!h) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    /* No GetWindowTextA. It SENDS WM_GETTEXT to the window own thread, and
     * every window here belongs to a thread that may never pump -- the
     * loading panel and the host window both belong to the main thread,
     * which is inside lifted code from the entry point until the game exits.
     * The first version of this report hung on its first row. GetClassNameA,
     * GetWindowRect and IsWindowVisible all read shared state instead. */
    char cls[64] = {0};
    GetClassNameA(h, cls, sizeof cls);
    RECT r = {0, 0, 0, 0};
    GetWindowRect(h, &r);
    fprintf(stderr, "[window] %p %s class=\"%s\" at %ld,%ld %ldx%ld\n",
            (void*)h, IsWindowVisible(h) ? "VISIBLE" : "hidden ", cls,
            r.left, r.top, r.right - r.left, r.bottom - r.top);
    return TRUE;
}

static DWORD WINAPI windowlist(LPVOID unused) {
    (void)unused;
    Sleep(g_wl_ms);
    fprintf(stderr, "[window] --- windows at %lu ms\n", g_wl_ms);
    /* The three by name first, because EnumWindows came back empty for this
     * process and a report that says nothing is worse than no report. */
    extern void* g_panel_hwnd;                   /* shims_impl.c */
    void* win_main_hwnd(void);                   /* shims_impl.c */
    window_row((HWND)g_panel_hwnd, 0);
    window_row((HWND)win_main_hwnd(), 0);
    window_row(g_hwnd, 0);
    fprintf(stderr, "[window] --- and every top-level window\n");
    EnumWindows(window_row, 0);
    return 0;
}

static DWORD    g_tl_ms;
static int      g_tl_on;

/*
 * --threadwatch MS N: the same census, N times, running threads only.
 *
 * "Which page am I on" is a question the thread records answer better than
 * the screen does, because the screen's text is not legible and the records
 * have names. A thread with a compiled block and a context is live, so a
 * snapshot every few seconds through a click chain is a timeline of the front
 * end's state -- Opening Screen, then whatever New Player starts, then
 * whatever Play starts.
 */
static DWORD g_tw_every;
static unsigned g_tw_count;

static void threadlist_once(int running_only) {
    uint32_t top = crt_heap_top();
    unsigned n = 0;
    /* A megabyte at a time with a yield between slices. Run flat out, the
     * scan holds a core for ten seconds and the game's own threads -- which
     * share one cooperative machine -- reorder enough to expose a race that
     * faults in its renderer. The scan is read-only; the starvation was the
     * problem. */
    for (uint32_t a = FOCOM_HEAP_BASE; a + 0x24 <= top; a += 4) {
        if ((a & 0xFFFFFu) == 0) Sleep(1);
        if (!vx_readable(a, 0x24, top)) continue;
        uint32_t blk = MEM32(a + 0x10);
        if (!vx_readable(blk, 0x20, top) || MEM32(blk) != VIS_CODEBLOCK_VTBL)
            continue;
        uint32_t nm = MEM32(a);
        if (!vx_readable(nm, 33, top)) continue;
        char buf[33];
        int j = 0;
        for (; j < 32; j++) {
            uint8_t c = MEM8(nm + (uint32_t)j);
            if (c == 0) break;
            if (c < 0x20 || c > 0x7E) { j = -1; break; }
            buf[j] = (char)c;
        }
        if (j < 1) continue;
        buf[j] = 0;
        uint32_t lines = MEM32(blk + 0x1C), ctx = MEM32(a + 0x20);
        if (running_only && (ctx == 0xFFFFFFFFu || lines == 0)) continue;
        fprintf(stderr, "[thread] %-32s block=%08X lines=%-5u container=%08X"
                        " index=%-5u ctx=%08X\n",
                buf, blk, lines, MEM32(a + 0xC),
                MEM32(a + 0x1C), ctx);
        n++;
    }
    fprintf(stderr, "[thread] %u script threads (heap to %08X)\n", n, top);
}

static DWORD WINAPI threadlist(LPVOID unused) {
    (void)unused;
    Sleep(g_tl_ms);
    threadlist_once(0);
    return 0;
}

static DWORD WINAPI threadwatch(LPVOID unused) {
    (void)unused;
    for (unsigned k = 0; k < g_tw_count; k++) {
        Sleep(g_tw_every);
        fprintf(stderr, "[thread] --- snapshot %u at %lu ms\n",
                k + 1, (unsigned long)g_tw_every * (k + 1));
        threadlist_once(1);
    }
    return 0;
}

static DWORD WINAPI varxref(LPVOID unused) {
    (void)unused;
    Sleep(g_vx_ms);
    uint32_t top = crt_heap_top();
    uint32_t bases[VX_BASES];
    unsigned nbases = 0, hits = 0;

    for (uint32_t a = FOCOM_HEAP_BASE; a + 32 <= top; a += 4) {
        if (!vx_readable(a, 32, top)) continue;
        uint32_t fn = MEM32(a);
        if (!vx_is_thunk(fn)) continue;
        uint32_t args = MEM32(a + 8);
        if (!vx_readable(args, 0x60, top)) continue;
        int n = (int)((MEM32(args) >> 16) & 0x3Fu) - 3;
        /*
         * A line with no operand LIST keeps its single node in the argument
         * block itself: the word at args+0 and the slot at args+8, which is
         * what sub_00506000 reads. Those are the simple lines -- a Set, a
         * plain read -- and leaving them out of this scan is how the first
         * version reported four references when there were more.
         */
        /*
         * args+0 is the line's OWN node and args+8 its slot -- the
         * DESTINATION for a line that assigns -- whether or not the line also
         * carries an operand list. Checking it only when the list was empty
         * was a hole: it is how a search for slot 83 came back with three
         * readers and no writer.
         */
        {
            if ((MEM32(args) & 0xF000u) == 0x1000u
                && MEM32(args + 8) == g_vx_slot) {
                uint32_t obj = MEM32(a + 4);
                uint32_t inner = T_OK(obj) ? MEM32(obj + 0x20) : 0;
                uint32_t base = a;
                while (base >= FOCOM_HEAP_BASE + 32
                       && vx_is_thunk(MEM32(base - 32)))
                    base -= 32;
                unsigned b;
                for (b = 0; b < nbases; b++) if (bases[b] == base) break;
                if (b == nbases && nbases < VX_BASES) bases[nbases++] = base;
                fprintf(stderr, "[varxref] slot %u at line %08X: thunk %08X"
                                " ivt=%08X DEST sub=%u array %08X"
                                " index %u\n",
                        g_vx_slot, a, fn, T_OK(inner) ? MEM32(inner) : 0,
                        MEM32(args) & 0xFFFu, base, (a - base) / 32);
                hits++;
            }
        }
        if (n <= 0 || n > 24) continue;
        for (int i = 0; i < n; i++) {
            uint32_t node = args + 0xC + (uint32_t)i * 12;
            if (!T_OK(node + 8)) break;
            if ((MEM32(node) & 0xF000u) != 0x1000u) continue;
            if (MEM32(node + 8) != g_vx_slot) continue;

            uint32_t base = a;
            while (base >= FOCOM_HEAP_BASE + 32 && vx_is_thunk(MEM32(base - 32)))
                base -= 32;
            unsigned b;
            for (b = 0; b < nbases; b++) if (bases[b] == base) break;
            if (b == nbases && nbases < VX_BASES) bases[nbases++] = base;
            /* [line+4] is the GamePPVisLibrary wrapper and the object that
             * implements this particular script function is at wrapper+0x20,
             * whose vtable analysis/rtti.json names -- so the hit says WHICH
             * script function mentions the slot, not just where. */
            uint32_t obj = MEM32(a + 4);
            uint32_t inner = T_OK(obj) ? MEM32(obj + 0x20) : 0;
            fprintf(stderr, "[varxref] slot %u at line %08X: thunk %08X"
                            " ivt=%08X operand %d/%d sub=%u array %08X"
                            " index %u\n",
                    g_vx_slot, a, fn,
                    T_OK(inner) ? MEM32(inner) : 0,
                    i, n, MEM32(node) & 0xFFFu, base, (a - base) / 32);
            hits++;
            break;
        }
    }

    uint32_t blocks[VX_BASES];
    unsigned nblocks = 0;

    /* Second pass: name the blocks those line arrays belong to. */
    for (uint32_t a = FOCOM_HEAP_BASE; a + 0x20 <= top; a += 4) {
        if (!vx_readable(a, 0x40, top)) continue;
        uint32_t arr = MEM32(a + 0x18), cnt = MEM32(a + 0x1C);
        if (!T_OK(arr) || cnt == 0 || cnt > 4096) continue;
        for (unsigned b = 0; b < nbases; b++)
            if (bases[b] == arr) {
                fprintf(stderr, "[varxref]   array %08X is block %08X"
                                " with %u lines, header:", arr, a, cnt);
                for (int k = 0; k < 0x40; k += 4)
                    fprintf(stderr, " %08X", MEM32(a + k));
                fprintf(stderr, "\n");
                if (nblocks < VX_BASES) blocks[nblocks++] = a;
            }
    }
    /*
     * Third pass: who points AT those blocks.
     *
     * A block only steps when something calls the container step with an
     * execution context, so a block that exists and never runs is one nobody
     * invokes. Every dword in the heap equal to the block's address is a
     * candidate holder -- an event-function object, a thread, a page -- and
     * its neighbours say which.
     */
    for (uint32_t a = FOCOM_HEAP_BASE; a + 4 <= top; a += 4) {
        uint32_t v = MEM32(a);
        for (unsigned b = 0; b < nblocks; b++) {
            if (v != blocks[b]) continue;
            fprintf(stderr, "[varxref]   block %08X held at %08X, around:",
                    v, a);
            for (int k = -0x10; k < 0x14; k += 4)
                fprintf(stderr, " %08X", MEM32((uint32_t)((int32_t)a + k)));
            /* The holder records seen so far start with a name pointer four
             * dwords back, so print anything there that reads as text. */
            for (int k = -0x10; k < 0x14; k += 4) {
                uint32_t sp = MEM32((uint32_t)((int32_t)a + k));
                if (!T_OK(sp)) continue;
                char nm[33];
                int j = 0;
                for (; j < 32; j++) {
                    uint8_t c = MEM8(sp + (uint32_t)j);
                    if (c == 0) break;
                    if (c < 0x20 || c > 0x7E) { j = -1; break; }
                    nm[j] = (char)c;
                }
                if (j >= 3) { nm[j] = 0; fprintf(stderr, "  [%+d]=\"%s\"", k, nm); }
            }
            fprintf(stderr, "\n");
        }
    }

    fprintf(stderr, "[varxref] %u lines reference slot %u,"
                    " in %u blocks (heap to %08X)\n",
            hits, g_vx_slot, nbases, top);
    return 0;
}

static int      g_varpoke_on;
static uint32_t g_varpoke_slot, g_varpoke_val;

static DWORD WINAPI scripttrace_window(LPVOID unused) {
    (void)unused;
    Sleep(g_st_from);
    g_scripttrace = 1;
    fprintf(stderr, "[scripttrace] on at %lu ms\n", g_st_from);
    if (g_st_for) {
        Sleep(g_st_for);
        g_scripttrace = 0;
        fprintf(stderr, "[scripttrace] off after %lu ms\n", g_st_for);
    }
    return 0;
}

/*
 * sub_005127E0 is GamePPSysCodeContainer's "run the current line":
 *
 *     if ([ctx+0x2C] >= [this+0x1C]) return 0;        // past the last line
 *     entry = [this+0x18] + [ctx+0x2C] * 32;
 *     return entry->fn(ctx, entry);
 *
 * so ecx is the code block, [ecx+0x18] its 32-byte line array and [ecx+0x1C]
 * its line count, and the first argument is the execution context whose
 * lineNumber_ is at +0x2C. Logging the pair says which line a block stopped
 * on and whether it ran out or jumped away -- which no amount of reading the
 * bytecode settles, because the loop opcodes are resolved to line numbers at
 * compile time.
 *
 * sub_00512170 is one of the per-line thunks; [line+4] is the
 * GamePPSysLibraryObj that implements it and [[line+4]] its vtable, which
 * analysis/rtti.json maps to a class name.
 */
static void focom_trace_extra(uint32_t va) {
    if (!g_scripttrace && !g_nolibtrace && !g_varpoke_on
        && g_nodedump < 0 && g_nocond_lines < 0) return;
    /*
     * GamePPVisLibraryManager::GetLibrary(id) is a bounds check and one load
     * from a 1024-entry table at [this+8] -- see sub_0052C300. A script line
     * that names a subsystem the exe never registered therefore gets NULL
     * back, silently, and whatever the line was going to do does not happen.
     * Reading the same slot here says which ids are missing, which no static
     * pass can: the registration is 8,662 call sites of a two-hop pattern.
     */
    if (va == GET_LIBRARY) {
        uint32_t id = MEM32(g_esp + 4);
        uint32_t tab = g_ecx >= 0x00200000u ? MEM32(g_ecx + 8) : 0;
        uint32_t lib = (tab >= 0x00200000u && id < 1024) ? MEM32(tab + id * 4) : 0;
        /* Once per id: the same missing subsystem is asked for every frame. */
        static uint8_t said[1024];
        if (!lib && id < 1024 && !said[id]) {
            said[id] = 1;
            fprintf(stderr, "[nolib] t%lu GetLibrary(%u) -> NULL\n",
                    GetCurrentThreadId(), id);
        }
        return;
    }
    if (!g_scripttrace && !g_varpoke_on && g_nodedump < 0
        && g_nocond_lines < 0) return;
    if (va == VIS_STEP) {
        uint32_t ctx = MEM32(g_esp + 4);
        uint32_t n = (g_ecx >= 0x00200000u) ? MEM32(g_ecx + 0x1C) : 0;
        uint32_t ln = (ctx >= 0x00200000u) ? MEM32(ctx + 0x2C) : 0xFFFFFFFFu;
        /* The line record is 32 bytes; [entry+4] is the library object that
         * implements it and [[entry+4]] its vtable, which names the class. */
        uint32_t entry = (g_ecx >= 0x00200000u && (int)ln >= 0 && ln < n)
                       ? MEM32(g_ecx + 0x18) + ln * 32 : 0;
        uint32_t obj = entry ? MEM32(entry + 4) : 0;
        uint32_t vt = T_OK(obj) ? MEM32(obj) : 0;
        /* [entry+4] is always the GamePPVisLibrary WRAPPER, whose vtable is the
         * same for every line and therefore says nothing. The object that
         * implements this particular script function is at wrapper+0x20 -- see
         * sub_00517FA0, which is nothing but a forward to it -- and ITS vtable
         * is a class per script function, which rtti.json names. */
        uint32_t inner = T_OK(obj) ? MEM32(obj + 0x20) : 0;
        uint32_t ivt = T_OK(inner) ? MEM32(inner) : 0;
        /* [entry+8] is the line's argument block and [args+8] the opcode the
         * subsystem switches on -- GamePPGlobalThread's Execute is a 44-way
         * jump table on exactly this value, so it says which thread operation
         * a line is. */
        uint32_t args = entry ? MEM32(entry + 8) : 0;
        uint32_t op = T_OK(args) ? MEM32(args + 8) : 0xFFFFFFFFu;
        /*
         * And the argument NODE, which is what says where a condition reads
         * from. sub_00655280 -- the handler [line+4] points at for the
         * sub_005120D0 and sub_00512060 thunks -- takes (ctx, args, ...),
         * reads the 16-bit word at args+0 and dispatches on its top nibble
         * through a four-entry table at 0x007C45D8:
         *
         *   0  a script function call produces the value (sub_00506150)
         *   1  a VARIABLE: [[ctx+0x14]+0x18] + [args+8]*4   (sub_00506000)
         *   2  an immediate: [args+8]                       (sub_00506020)
         *   3  inline data at args+4                        (sub_00506030)
         *
         * So for type 1 the slot address and its current value are both
         * computable here, and a condition that never changes says which
         * dword in the target never changes -- which --poison can then watch
         * and --poke can force.
         */
        /* The WHOLE word. Its low twelve bits are the subsystem id and its
         * top nibble the access kind, but the high half is the declaration
         * INDEX into the script's own table -- which is the only part that
         * names the variable. tools/gtxvars.py resolves it. */
        uint32_t node = T_OK(args) ? MEM32(args) : 0;
        uint32_t vaddr = 0, vval = 0;
        if (((node & 0xF000u) >> 12) == 1 && T_OK(ctx)) {
            uint32_t owner = MEM32(ctx + 0x14);
            uint32_t arr = T_OK(owner) ? MEM32(owner + 0x18) : 0;
            if (T_OK(arr)) {
                vaddr = arr + (op & 0xFFFFu) * 4;
                vval = T_OK(vaddr) ? MEM32(vaddr) : 0;
            }
        }
        /* Line count and line number alone are not an identity: this front
         * end has several 25-line blocks and the first one reached got the
         * patch. Operand 0's slot pins it -- for the "For CD" While that is
         * variable 83, "Min CD Number". */
        int nocond_here = !g_nocond_done && (int)n == g_nocond_lines
                       && (int)ln == g_nocond_line && T_OK(args);
        if (nocond_here && g_nocond_slot >= 0) {
            uint32_t op0 = args + 0xC;
            nocond_here = (int)((MEM32(args) >> 16) & 0x3Fu) - 3 > 0
                       && T_OK(op0 + 8)
                       && (MEM32(op0) & 0xF000u) == 0x1000u
                       && (int)MEM32(op0 + 8) == g_nocond_slot;
        }
        if (nocond_here) {
            uint32_t was = MEM32(args);
            MEM32(args) = (was & ~0x003F0000u) | (3u << 16);
            g_nocond_done = 1;
            fprintf(stderr, "[nocond] block %08X line %d of %u:"
                            " header %08X -> %08X, %d operands -> 0\n",
                    g_ecx, (int)ln, n, was, MEM32(args),
                    (int)((was >> 16) & 0x3Fu) - 3);
        }
        if (g_nodedump >= 0 && (int)ln == g_nodedump && T_OK(args)) {
            /* [entry+0x18] and not [entry+8]: While::Execute takes three
             * arguments and it is the THIRD -- the line's extra field -- whose
             * [0] carries the operand count, [8] the jump target and +0xC the
             * operand list. [entry+8] is the argument block the non-control
             * lines use. Both are dumped because only one of them was ever
             * the right one. */
            uint32_t extra = MEM32(entry + 0x18);
            fprintf(stderr, "[node] block=%08X line=%d of %u"
                            " args=%08X extra=%08X count=%d:",
                    g_ecx, (int)ln, n, args, extra,
                    T_OK(extra) ? (int)(((MEM32(extra) >> 16) & 0x3Fu)) - 3 : -99);
            /* 0xC + 24 operands * 12 bytes covers the widest line seen.
             * Each operand is [word][0][slot] and the word is
             * [decl index:16][kind:4][subsystem id:12] -- the decl index is
             * what tools/gtxvars.py turns into a name. */
            int cnt = T_OK(extra) ? (int)((MEM32(extra) >> 16) & 0x3Fu) - 3 : 0;
            (void)cnt;
            for (int k = 0; k < 0x60; k += 4)
                fprintf(stderr, " %08X", MEM32(args + k));
            fprintf(stderr, " |");
            for (int k = 0; T_OK(extra) && k < 0x30; k += 4)
                fprintf(stderr, " %08X", MEM32(extra + k));
            fprintf(stderr, "\n");
        }
        /*
         * Pin the slot wherever this line mentions it, which is two different
         * places. A simple line keeps its slot at args+8 -- that is `op` --
         * and a line with an operand LIST keeps one per operand at
         * args+0xC+12i+8. The first version only looked at `op`, so
         * --varpoke 83 wrote nothing at all: every reference to slot 83 in
         * this front end is an operand, and for the While that matters most
         * `op` is the jump target.
         */
        if (g_varpoke_on && T_OK(ctx)) {
            uint32_t owner = MEM32(ctx + 0x14);
            uint32_t arr = T_OK(owner) ? MEM32(owner + 0x18) : 0;
            int cnt = T_OK(args) ? (int)((MEM32(args) >> 16) & 0x3Fu) - 3 : 0;
            if (T_OK(arr)) {
                uint32_t slots[25];
                unsigned ns = 0;
                if (cnt <= 0) {
                    if (T_OK(args) && (MEM32(args) & 0xF000u) == 0x1000u)
                        slots[ns++] = MEM32(args + 8);
                } else if (cnt <= 24) {
                    for (int k = 0; k < cnt; k++) {
                        uint32_t nd = args + 0xC + (uint32_t)k * 12;
                        if (T_OK(nd + 8) && (MEM32(nd) & 0xF000u) == 0x1000u)
                            slots[ns++] = MEM32(nd + 8);
                    }
                }
                for (unsigned k = 0; k < ns; k++) {
                    if ((slots[k] & 0xFFFFu) != g_varpoke_slot) continue;
                    uint32_t va2 = arr + (slots[k] & 0xFFFFu) * 4;
                    if (!T_OK(va2) || MEM32(va2) == g_varpoke_val) continue;
                    fprintf(stderr, "[varpoke] slot %u at %08X: %08X -> %08X"
                                    " (block %08X line %d of %u)\n",
                            g_varpoke_slot, va2, MEM32(va2), g_varpoke_val,
                            g_ecx, (int)ln, n);
                    MEM32(va2) = g_varpoke_val;
                    if (va2 == vaddr) vval = g_varpoke_val;
                }
            }
        }
        if (!g_scripttrace) return;
        fprintf(stderr, "[step] t%lu block=%08X line=%d of %u"
                        " fn=%08X vt=%08X ivt=%08X op=%d node=%04X"
                        " decl=%u var=%08X=%08X\n",
                GetCurrentThreadId(), g_ecx, (int)ln, n,
                entry ? MEM32(entry) : 0, vt, ivt, (int)op, node,
                node >> 16, vaddr, vval);
        return;
    }
    if (va != VIS_RUN_LINE) return;
    uint32_t line = MEM32(g_esp + 8);
    uint32_t obj = line >= 0x00200000u ? MEM32(line + 4) : 0;
    uint32_t vt = T_OK(obj) ? MEM32(obj) : 0;
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

/*
 * --nowait: answer GamePPGlobalSysWaitIf's condition with "no".
 *
 * The boot script ends with one context on Wait Forever and one on a Wait If
 * that never clears, and the section then retires with nothing ever presented.
 * Whether the wait is the whole story or merely the last symptom cannot be read
 * off the code -- the condition is a compiled script expression. So this
 * replaces sub_005D4350 (WaitIf::Execute, vtable 0x007D1408 slot 20) with a
 * "condition false" stub and lets the script run on.
 *
 * It is a probe, not a fix: a real game skips the intro on a keypress, it does
 * not stop evaluating its conditions. `ret 0xc` is three arguments, hence
 * STDRET(4).
 */
/*
 * --switchtrace: what GamePPGlobalSysSwitch is dispatching on, and what its
 * Cases compare against.
 *
 * sub_005D0990 evaluates the switch expression and stores it at [ctx+0x80],
 * where sub_005D0520 (Case) compares it; both take (library, ctx, line), so
 * the context is the SECOND stack argument and the line record the third. The
 * current line is [ctx+0x2C] and has to be read before calling through,
 * because Execute ends by advancing it. [ctx+0x24] is the code container and
 * [container+0x1C] its line count, which is the only way here to tell one
 * script block from another.
 *
 * What it found in the front end: the 309-line page controller switches on
 * the CURRENT page id, which is 7, at lines 63, 89 and 212, and its Cases
 * compare against 5, 11, 19 and 20. So "it takes the default" is not a
 * failure -- page 7 simply has no special case. That corrected an earlier
 * reading of exactly the same trace, which is why the probe is worth keeping.
 */
/*
 * --switchtrace also reports the Cases, because "none of them matched" is only
 * half an answer. sub_005D0520 takes (library, ctx, line) like the Switch, and
 * the case's constant is an expression node at line+0x10 -- so the node's
 * words go in the report and the switch value comes from [ctx+0x80].
 */
#define CASE_EXEC 0x005D0520u

static void focom_case_probe(void) {
    uint32_t ctx = MEM32(g_esp + 8), line = MEM32(g_esp + 0xC);
    int ln = T_OK(ctx) ? (int)MEM32(ctx + 0x2C) : -1;
    uint32_t val = T_OK(ctx) ? MEM32(ctx + 0x80) : 0;
    recomp_func_t real = recomp_lookup(CASE_EXEC);
    if (!real) { RET(0); STDRET(4); return; }
    real();
    /* [ctx+0x24] is the code container and [container+0x1C] its line count,
     * which is the only way to tell one script block from another here: the
     * front end's page controller is the 309-line one. */
    uint32_t blk = T_OK(ctx) ? MEM32(ctx + 0x24) : 0;
    uint32_t cnt = T_OK(blk) ? MEM32(blk + 0x1C) : 0;
    if (cnt == 309 && T_OK(line))
        fprintf(stderr, "[case] 309:line=%d switchval=%d node=%08X %08X %08X"
                        " -> line %d\n",
                ln, (int)val, MEM32(line + 0x10), MEM32(line + 0x14),
                MEM32(line + 0x18), T_OK(ctx) ? (int)MEM32(ctx + 0x2C) : -1);
}

#define SWITCH_EXEC 0x005D0990u
int g_switchtrace = 0;

static void focom_switch_probe(void) {
    /* sub_005D0990 reads its context from [esp+0x10] AFTER two pushes, which
     * is the SECOND stack argument, and the line record from the third. */
    uint32_t ctx = MEM32(g_esp + 8), arg1 = MEM32(g_esp + 0xC);
    /* The line has to be read BEFORE: Switch::Execute ends in sub_00507050,
     * which advances it. */
    int line = T_OK(ctx) ? (int)MEM32(ctx + 0x2C) : -1;
    recomp_func_t real = recomp_lookup(SWITCH_EXEC);
    if (!real) { RET(0); STDRET(4); return; }
    real();
    uint32_t blk = T_OK(ctx) ? MEM32(ctx + 0x24) : 0;
    uint32_t cnt = T_OK(blk) ? MEM32(blk + 0x1C) : 0;
    if (T_OK(ctx) && cnt == 309)
        fprintf(stderr, "[switch] 309:line=%d value=%d\n",
                line, (int)MEM32(ctx + 0x80));
    (void)arg1;
}

#define WAITIF_EXEC 0x005D4350u
int g_nowait = 0;
int g_waittrace = 0;

static void focom_waitif_false(void) { RET(0); STDRET(4); }

/*
 * --waittrace: call the real WaitIf and say what it decided.
 *
 * The interesting number is ctx->flags at +0x30 before and after: bit 2 set
 * means "waiting", and the line at +0x2C is where it will resume. A Wait If
 * that is evaluated ONCE and never again is the shape of an event-driven wait
 * -- sub_00506EC0 registers against a queue rather than polling -- so the
 * question a poll-shaped reading would ask ("why is the condition still true")
 * is the wrong one; the question is which post never happens.
 *
 * recomp_lookup goes to the generated dispatch table, which this override is
 * not in, so it returns the real body.
 */
static void focom_waitif_probe(void) {
    uint32_t ctx = MEM32(g_esp + 8);
    uint32_t ln = ctx >= 0x00200000u ? MEM32(ctx + 0x2C) : 0;
    uint32_t before = ctx >= 0x00200000u ? MEM32(ctx + 0x30) : 0;
    recomp_func_t real = recomp_lookup(WAITIF_EXEC);
    if (!real) { RET(0); STDRET(4); return; }
    real();
    uint32_t after = ctx >= 0x00200000u ? MEM32(ctx + 0x30) : 0;
    fprintf(stderr, "[waitif] ctx=%08X line=%u flags %08X -> %08X resume=%u%s\n",
            ctx, ln, before, after,
            ctx >= 0x00200000u ? MEM32(ctx + 0x2C) : 0,
            (after & 4) ? "  WAITING" : "");
}

/*
 * sub_00506EC0 is what WaitIf asks. Its first argument is not a value but an
 * OBJECT -- the condition expression evaluates to a reference, and the wait is
 * registered against it (the body calls [obj vtable + 0x78] and then walks the
 * context's event queue at [ctx+0x24]). So the vtable of that object names the
 * thing the boot script is waiting for, which is the whole question.
 */
#define WAIT_DECIDE 0x00506EC0u

static void focom_waitdecide_probe(void) {
    uint32_t obj = MEM32(g_esp + 4), cond = MEM32(g_esp + 8);
    fprintf(stderr, "[waiton] obj=%08X vt=%08X cond=%u ctx=%08X\n",
            obj, obj >= 0x00200000u ? MEM32(obj) : 0, cond, g_ecx);
    recomp_func_t real = recomp_lookup(WAIT_DECIDE);
    if (!real) { RET(0); STDRET(3); return; }
    real();
    fprintf(stderr, "[waiton] -> %u\n", g_eax & 0xFFu);
}

recomp_func_t recomp_lookup_manual(uint32_t va) {
    if (g_nowait && va == WAITIF_EXEC) return (recomp_func_t)focom_waitif_false;
    if (g_waittrace && va == WAITIF_EXEC)
        return (recomp_func_t)focom_waitif_probe;
    if (g_waittrace && va == WAIT_DECIDE)
        return (recomp_func_t)focom_waitdecide_probe;
    if (g_switchtrace && va == SWITCH_EXEC)
        return (recomp_func_t)focom_switch_probe;
    if (g_switchtrace && va == CASE_EXEC)
        return (recomp_func_t)focom_case_probe;
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

/* g_hwnd is declared up with --windows, which reports it. */
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

uint32_t win_main(uint32_t* target_hwnd);   /* shims_impl.c */

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

/*
 * Forward input to the game's own window procedure.
 *
 * The host window has the pixels; the game's window has the procedure. See
 * win_main() in shims_impl.c for why they are not the same window. Mouse and
 * key messages that arrive here are handed to the game's procedure with the
 * game's own HWND substituted, which is what its code compares against.
 *
 * lParam for the mouse messages is already a packed pair of client
 * coordinates, and both windows have a client area of the same size, so it
 * passes through unchanged.
 *
 * This is a leftover path now, and harmless. The host window is hidden as
 * soon as the game's exists (see host_present), so Windows delivers input
 * straight to the game's own window on the thread that pumps it, which is
 * what should happen. It stays for the window that exists BEFORE the game
 * makes its own -- and because nothing pumps the host queue anyway, the main
 * thread being inside lifted code for the whole run.
 *
 * ponytail: mouse and keys only. No WM_SETFOCUS, no WM_ACTIVATE, no capture,
 * no WM_CHAR translation -- add them when something is observed to want one.
 */
static int host_input(UINT m, WPARAM w, LPARAM l) {
    uint32_t proc, thwnd;
    switch (m) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MOUSEWHEEL:
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        break;
    default:
        return 0;
    }
    proc = win_main(&thwnd);
    recomp_func_t f = proc ? recomp_lookup(proc) : NULL;
    if (!proc || !thwnd || !f) return 0;
    uint32_t save_esp = g_esp;
    call_lifted_stdcall4(f, thwnd, m, (uint32_t)w, (uint32_t)l);
    if (g_esp != save_esp) {
        fprintf(stderr, "[stack] wndproc msg 0x%04X left esp at 0x%08X, "
                        "expected 0x%08X\n", m, g_esp, save_esp);
        g_esp = save_esp;
    }
    return 1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    uint32_t r = 0;
    if (host_input(m, w, l)) return 0;
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
uint32_t host_create_window(const char* title, int show) {
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

    /* Shown only when it is the window the user will look at. Running the
     * game, it is not: the game makes its own, Windows delivers input there,
     * and host_present blits the frames there. Two windows -- one fullscreen
     * and white with the game's loading panel, one small with the actual
     * frames -- was what the split looked like from outside. */
    if (show) ShowWindow(g_hwnd, SW_SHOW);
    return (uint32_t)(uintptr_t)g_hwnd;
}

/*
 * Blit straight to the window, rather than invalidating and calling
 * UpdateWindow.
 *
 * UpdateWindow sends WM_PAINT, and SendMessage to a window owned by ANOTHER
 * thread blocks until that thread pumps. The game's renderer runs on a Ronin
 * worker and the window belongs to the main thread, which at that moment is
 * inside its own wait -- so the first Flip the game ever issued hung the
 * process, and the watchdog reported "no lifted call for 90 s" with
 * CDD7FSScreen::Present at the top of the entry trace.
 *
 * GetDC/BitBlt/ReleaseDC on another thread's window is allowed and does not
 * wait for anybody. WM_PAINT still repaints from the same DIB on expose.
 *
 * ponytail: no lock around g_memdc, so a WM_PAINT on the main thread can blit
 * from it while a worker blits from it too. Both are reads of the same bits
 * and the worst case is one torn frame. Add a critical section if tearing ever
 * matters more than the frame rate.
 */
/*
 * One window, and it is the game's.
 *
 * There were two, and they were visibly two: a fullscreen white one with
 * "Force Commander" in yellow at the bottom -- the game's own window, with its
 * GDI loading panel -- and a smaller one beside it with the actual rendered
 * frames. The host makes the second because something has to exist before the
 * game calls CreateWindowExA, and the frames were going there while every
 * mouse and key message Windows delivered went to the game's.
 *
 * So the moment the game's window exists, that is where the frames go, and
 * running the game no longer shows the host's window at all. Presenting is a
 * GetDC/BitBlt/ReleaseDC from whichever thread flipped, which is allowed
 * cross-thread and waits for nobody -- unlike UpdateWindow, which hung the
 * first Flip the game ever issued, because SendMessage to another thread's
 * window blocks until that thread pumps.
 *
 * Hiding the host window from here was tried and is the same trap by another
 * door: ShowWindow SENDS WM_SHOWWINDOW, the host window belongs to the main
 * thread, and the main thread is inside lifted code from the entry point
 * until the game exits. It hung on the first present, with
 * CDD7FSScreen::Present at the top of the entry trace. So the host window is
 * simply never shown when the game is going to make its own -- see the `show`
 * argument to host_create_window.
 *
 * StretchBlt rather than BitBlt: the game's window is sized for the exclusive
 * fullscreen mode it asked for, and the rendered surface is 640x480, so the
 * two are only the same size by accident. StretchBlt with an equal source and
 * destination is a BitBlt.
 */
void* win_main_hwnd(void);               /* shims_impl.c */

void host_present(void) {
    HWND target = (HWND)win_main_hwnd();
    if (!target) target = g_hwnd;
    if (!target) return;
    HDC dc = GetDC(target);
    if (!dc) return;
    RECT c;
    if (!GetClientRect(target, &c) || c.right <= 0 || c.bottom <= 0) {
        c.left = c.top = 0; c.right = g_w; c.bottom = g_h;
    }
    if (c.right == g_w && c.bottom == g_h)
        BitBlt(dc, 0, 0, g_w, g_h, g_memdc, 0, 0, SRCCOPY);
    else
        StretchBlt(dc, 0, 0, c.right, c.bottom,
                   g_memdc, 0, 0, g_w, g_h, SRCCOPY);
    ReleaseDC(target, dc);
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
/*
 * --click and --key in ARGV ORDER, as one list.
 *
 * They used to be two lists, all the clicks and then all the keys, and a
 * front-end flow does not work like that: Single Player, then New Player,
 * then type a name, then the forward arrow. The order on the command line is
 * the order of the actions.
 */
#define CLICK_MAX 24
enum { ACT_CLICK = 1, ACT_KEY };
static struct { int kind, x, y; } g_click[CLICK_MAX];
static unsigned g_click_n;
static DWORD g_click_ms = 20000;                 /* --clickat MS */
static DWORD g_click_gap = 2500;                 /* --clickgap MS */
static double g_click_scale = 1.0;               /* --mousescale N */

static unsigned g_click_taps = 1;                /* --taps N */
static DWORD    g_click_hold = 300;              /* --hold MS */
static long   g_mouse_at_x, g_mouse_at_y;        /* where we last aimed */
void ddraw_mouse_move(long dx, long dy);         /* ddraw_shims.c */
void ddraw_mouse_button(int down);               /* ddraw_shims.c */
void ddraw_key(unsigned scancode, int down);     /* ddraw_shims.c */
void ddraw_uimap_reset(void);                    /* ddraw_shims.c */

/*
 * --click X Y: press the left button at a client position, once.
 *
 * PostMessage is the whole trick. It is thread-safe, it does not block, and
 * the messages land in the host window's own queue, so they come back out
 * through the pump and through host_input() into the game's window procedure
 * exactly as a real click would. Feeding DirectInput deltas instead was tried
 * first and moved nothing: the front end does not take its cursor from the
 * device.
 *
 * A move before the press, because a UI that tracks hover needs to know where
 * the pointer is before it is told a button went down.
 *
 * This exists so a run can reach the game with nobody at the keyboard, which
 * is what a check needs. It is not input support -- that is host_input(), and
 * it works with a real mouse.
 */
static DWORD WINAPI clicker(LPVOID unused) {
    (void)unused;
    void* win_main_hwnd(void);           /* shims_impl.c */
    Sleep(g_click_ms);
    /* The GAME's window, not the host's. Posting to the host window put the
     * messages in the main thread's queue, and the main thread is inside
     * lifted code for the whole run -- nobody pumps it, so they sat there.
     * The game's window belongs to the Ronin worker that created it, and that
     * worker's own step function pumps every frame (sub_00550F60), so a
     * message posted there is delivered to the game's procedure through
     * win_trampoline within a frame. */
    HWND h = (HWND)win_main_hwnd();
    if (!h) { fprintf(stderr, "[click] no game window\n"); return 0; }
    /* Home the cursor into the top-left corner first. The device is relative,
     * so there is no absolute position to set: the only way to a known place
     * is a delta big enough to clamp, then a delta out to the target. One
     * delta per frame, because the poller drains the device every frame. */
    for (int k = 0; k < 30; k++) { ddraw_mouse_move(-4000, -4000); Sleep(40); }

    for (unsigned k = 0; k < g_click_n; k++) {
        if (g_click[k].kind == ACT_KEY) {
            unsigned vk = (unsigned)g_click[k].x;
            ddraw_uimap_reset();
            /* All three paths, because the front end has an "Ascii Keys"
             * thread and a Keyboard subsystem and it is not yet known which
             * it reads: the scan code into the DirectInput key array, and
             * WM_KEYDOWN plus the WM_CHAR that TranslateMessage would have
             * produced plus WM_KEYUP to the window procedure, which
             * dispatches 0x100..0x112. */
            UINT sc = MapVirtualKeyA(vk, 0 /* MAPVK_VK_TO_VSC */);
            fprintf(stderr, "[key] vk 0x%02X scan 0x%02X\n", vk, sc);
            ddraw_key(sc, 1);
            PostMessageA(h, WM_KEYDOWN, vk, (LPARAM)(1 | (sc << 16)));
            UINT ch = MapVirtualKeyA(vk, 2 /* MAPVK_VK_TO_CHAR */) & 0xFFFF;
            if (ch) PostMessageA(h, WM_CHAR, ch, (LPARAM)(1 | (sc << 16)));
            Sleep(g_click_hold);
            ddraw_key(sc, 0);
            PostMessageA(h, WM_KEYUP, vk,
                         (LPARAM)(0xC0000001u | (sc << 16)));
            Sleep(g_click_gap);
            continue;
        }
        LPARAM pos = (LPARAM)((g_click[k].y << 16) | (g_click[k].x & 0xFFFF));
            if (k == 0) {
            /* Tell the game it is active first. A window that never got
             * WM_ACTIVATEAPP is one a 1999 title is entitled to assume is in
             * the background, and background input is exactly what a
             * DirectInput foreground-exclusive device is supposed to drop. */
            PostMessageA(h, WM_ACTIVATEAPP, TRUE, 0);
            PostMessageA(h, WM_ACTIVATE, WA_ACTIVE, 0);
            PostMessageA(h, WM_SETFOCUS, 0, 0);
            PostMessageA(h, WM_NCACTIVATE, TRUE, 0);
            Sleep(500);
        }
        ddraw_uimap_reset();
        fprintf(stderr, "[click] #%u at %d,%d (window %p)\n",
                k + 1, g_click[k].x, g_click[k].y, (void*)h);
        /* Window messages too: the procedure ignores the mouse ones, but
         * costing nothing and being the real path if that ever changes. */
        PostMessageA(h, WM_MOUSEMOVE, 0, pos);
        /* And the device, which is what the game actually reads. Relative, so
         * move from wherever the last click left it. */
        ddraw_mouse_move((long)(g_click[k].x * g_click_scale) - g_mouse_at_x,
                         (long)(g_click[k].y * g_click_scale) - g_mouse_at_y);
        g_mouse_at_x = (long)(g_click[k].x * g_click_scale);
        g_mouse_at_y = (long)(g_click[k].y * g_click_scale);
        Sleep(400);
        /* A press and a release, g_click_taps times. One selects the item and
         * shows its description; whether one also activates it has not been
         * settled, so this is adjustable rather than assumed. */
        for (unsigned tap = 0; tap < g_click_taps; tap++) {
            PostMessageA(h, WM_LBUTTONDOWN, MK_LBUTTON, pos);
            ddraw_mouse_button(1);
            Sleep(g_click_hold);
            PostMessageA(h, WM_LBUTTONUP, 0, pos);
            ddraw_mouse_button(0);
            if (tap + 1 < g_click_taps) Sleep(g_click_hold);
        }
        Sleep(g_click_gap);
    }
    fprintf(stderr, "[click] done\n");
    return 0;
}

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
    fprintf(stderr, "last %d indirect calls (target <- caller):\n",
            ICALL_TRACE_SIZE);
    for (int i = ICALL_TRACE_SIZE; i > 0; i--) {
        uint32_t idx = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        if (g_icall_trace[idx])
            fprintf(stderr, "  0x%08X <- 0x%08X\n",
                    g_icall_trace[idx], g_icall_from[idx]);
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
        else if (!strcmp(argv[i], "--dumpframe") && i + 1 < argc)
            ddraw_set_dumpframe(argv[++i]);
        else if (!strcmp(argv[i], "--click") && i + 2 < argc
                 && g_click_n < CLICK_MAX) {
            g_click[g_click_n].x = (int)strtol(argv[i + 1], NULL, 0);
            g_click[g_click_n].y = (int)strtol(argv[i + 2], NULL, 0);
            g_click[g_click_n].kind = ACT_CLICK;
            g_click_n++;
            i += 2;
        }
        else if (!strcmp(argv[i], "--clickat") && i + 1 < argc)
            g_click_ms = (DWORD)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--uimap")) ddraw_set_uimap();
        else if (!strcmp(argv[i], "--drawprobe") && i + 2 < argc) {
            int px = (int)strtol(argv[++i], NULL, 0);
            ddraw_set_drawprobe(px, (int)strtol(argv[++i], NULL, 0));
        }
        else if (!strcmp(argv[i], "--clickgap") && i + 1 < argc)
            g_click_gap = (DWORD)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--mousescale") && i + 1 < argc)
            g_click_scale = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--key") && i + 1 < argc
                 && g_click_n < CLICK_MAX) {
            g_click[g_click_n].kind = ACT_KEY;
            g_click[g_click_n].x = (int)strtol(argv[++i], NULL, 0);
            g_click_n++;
        }
        else if (!strcmp(argv[i], "--taps") && i + 1 < argc)
            g_click_taps = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
            g_click_hold = (DWORD)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--nowait")) g_nowait = 1;
        else if (!strcmp(argv[i], "--waittrace")) g_waittrace = 1;
        else if (!strcmp(argv[i], "--switchtrace")) g_switchtrace = 1;
        else if (!strcmp(argv[i], "--nothreads")) g_no_threads = 1;
        else if (!strcmp(argv[i], "--stubs")) g_list_stubs = 1;
        else if (!strcmp(argv[i], "--threadtrace")) g_threadtrace = 1;
        else if (!strcmp(argv[i], "--scripttrace")) g_scripttrace = 1;
        else if (!strcmp(argv[i], "--nolib")) g_nolibtrace = 1;
        else if (!strcmp(argv[i], "--windows") && i + 1 < argc) {
            g_wl_on = 1;
            g_wl_ms = (DWORD)strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--threadwatch") && i + 2 < argc) {
            g_tw_every = (DWORD)strtoul(argv[++i], NULL, 0);
            g_tw_count = (unsigned)strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--threadlist") && i + 1 < argc) {
            g_tl_on = 1;
            g_tl_ms = (DWORD)strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--varxref") && i + 2 < argc) {
            g_vx_on = 1;
            g_vx_slot = (uint32_t)strtoul(argv[++i], NULL, 0);
            g_vx_ms = (DWORD)strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--nocond") && i + 3 < argc) {
            g_nocond_lines = (int)strtol(argv[++i], NULL, 0);
            g_nocond_line = (int)strtol(argv[++i], NULL, 0);
            g_nocond_slot = (int)strtol(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--nodedump") && i + 1 < argc)
            g_nodedump = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--varpoke") && i + 2 < argc) {
            g_varpoke_on = 1;
            g_varpoke_slot = (uint32_t)strtoul(argv[++i], NULL, 0);
            g_varpoke_val = (uint32_t)strtoul(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--scripttracefrom") && i + 1 < argc)
            g_st_from = (DWORD)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--scripttracefor") && i + 1 < argc)
            g_st_for = (DWORD)strtoul(argv[++i], NULL, 0);
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
    if (g_click_n)
        CloseHandle(CreateThread(NULL, 0, clicker, NULL, 0, NULL));
    if (g_vx_on) CloseHandle(CreateThread(NULL, 0, varxref, NULL, 0, NULL));
        if (g_tl_on)
            CloseHandle(CreateThread(NULL, 0, threadlist, NULL, 0, NULL));
        if (g_tw_count)
            CloseHandle(CreateThread(NULL, 0, threadwatch, NULL, 0, NULL));
        if (g_wl_on)
            CloseHandle(CreateThread(NULL, 0, windowlist, NULL, 0, NULL));
        if (g_st_from) CloseHandle(CreateThread(NULL, 0,
                scripttrace_window, NULL, 0, NULL));

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
        host_create_window("STAR WARS: Force Commander", 1);
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

    recomp_trace_extra = focom_trace_extra;
    host_create_window("Force Commander (recomp)", 0);

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
