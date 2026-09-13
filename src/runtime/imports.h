/*
 * Force Commander recompilation - import bridge helpers.
 *
 * Shims run inside the recomp register/stack model. When lifted code calls an
 * import, RECOMP_ICALL has already pushed a dummy return address, so from ESP
 * the stack reads: [ret][arg0][arg1]... Each argument is one 32-bit slot.
 *
 *   ARG(n)     read argument n (0-based)
 *   ARGP(n,T)  read argument n as a host pointer of type T* (maps VA -> host)
 *   RET(v)     set the return value (EAX)
 *   STDRET(k)  stdcall/thiscall epilogue: drop the dummy ret addr + k slots
 *   CDECLRET() cdecl epilogue: drop the dummy ret addr only -- the CALLER
 *              cleans the arguments, so popping them here would unbalance
 *              the stack by exactly the argument count
 *
 * The cdecl/stdcall split is why this file differs from fury3's: Fury3 imports
 * no cdecl functions, Force Commander imports 135 of them (MSVCRT and the
 * __cdecl half of MSVCP60), and using STDRET for those would corrupt the stack.
 */
#ifndef FOCOM_IMPORTS_H
#define FOCOM_IMPORTS_H

#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"

#define ARG(n)      MEM32(g_esp + 4 + (n)*4)
#define ARGP(n, T)  ((T*)(void*)(uintptr_t)ADDR(ARG(n)))
#define RET(v)      do { g_eax = (uint32_t)(v); } while (0)
#define STDRET(k)   do { g_esp += 4 + (uint32_t)(k)*4; } while (0)
#define CDECLRET()  do { g_esp += 4; } while (0)

/* Log an unimplemented import once, by name. */
#define IMPORT_STUB(nm) do { \
    static int _w = 0; \
    if (!_w) { fprintf(stderr, "[import-stub] %s\n", (nm)); _w = 1; } \
} while (0)

/*
 * An import whose stack-purge count could not be derived from any evidence on
 * the machine. There is no safe stub: returning without popping desynchronises
 * the simulated stack, and popping a guessed count desynchronises it silently.
 * So this aborts, naming the symbol and its calling convention, which is the
 * whole point -- it says exactly which import to hand-write next.
 */
#define IMPORT_REFUSED(sym, conv) do { \
    fprintf(stderr, "\n[import-refused] %s (%s)\n" \
        "  Its stack-purge count is not derivable; guessing it would corrupt\n" \
        "  the stack with no visible cause. Hand-write it in shims_impl.c and\n" \
        "  add it to HOST_SHIM in gen_imports.py.\n", (sym), (conv)); \
    abort(); \
} while (0)

typedef void (*import_fn_t)(void);

typedef struct {
    uint32_t    iat_va;     /* the IAT slot the lifted code calls through */
    import_fn_t fn;
    const char* name;       /* "DLL!Symbol", for diagnostics */
} import_entry_t;

extern const import_entry_t g_imports[];
extern const unsigned       g_import_count;

#endif /* FOCOM_IMPORTS_H */
