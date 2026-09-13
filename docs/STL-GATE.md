# The MSVCP60 gate, and a case for a 32-bit host

Force Commander imports **82 functions from MSVCP60.dll**, and 58 of them are
`__thiscall` members of `basic_string`, `basic_filebuf`, `basic_ios`,
`basic_iostream`, `basic_streambuf`, `ios_base`, `locale` and `_Lockit`.

`_initterm` reaches them first, so nothing runs past static initialisation
without an answer here. This is the largest single obstacle between the splash
screen and a mission.

---

## Why they are refused rather than stubbed

A `__thiscall` callee pops its own arguments, so the purge count is load-bearing:
too few and the stack keeps the arguments, too many and it eats the caller's
frame. Either way the damage shows up somewhere else entirely.

`pe/stdcall_argc.py` resolves 248 of 307 imports and refuses these 58 on
purpose. What it can read from a mangled name is the calling convention; what it
cannot is the argument *byte* count, because that needs the parameter types and
a struct passed by value has no size recorded in the name.

## Deriving the counts is possible, and is not the hard part

Worth writing down because it looks like the obvious next move and it is not.

Every parameter in all 58 signatures is a single 4-byte slot -- `I`, `D`, `H`,
`_N`, `PBD`, `PAD`, `ABV12@`, `PAU_iobuf@@`. There is no by-value struct
anywhere in the set. The only subtlety is a by-value *return*: `?substr@...QBE?AV12@II@Z`
returns a `basic_string`, which cannot come back in `eax`, so MSVC passes a
hidden return-buffer pointer and the real count is 3, not 2.

So the counts are derivable. A prototype parser got 11 of 20 hand-checked cases
right, and the remaining failures were all one of three understood bugs:

- **Backref name components end in a single `@`, not `@@`.** `AAV12@` is
  reference → class → backref(1) → backref(2) → terminator. A skipper looking
  for `@@` swallows the whole parameter list.
- **Constructors and destructors have no return type.** `??1type_info@@UAE@XZ`
  goes straight from the convention letter to `@XZ`; parsing a return type there
  consumes a parameter.
- **Template components nest**, so skipping a name means recursing through
  `?$name@<types>@`.

Fixing those means writing a real MSVC demangler. And the reason not to is that
**the table has to be written by hand anyway**: knowing that `?erase@...` pops 2
slots does not tell you what `erase` *does*. Each of these 58 needs a real body,
and whoever writes the body knows the signature, so the count comes free with
the work that cannot be avoided. Deriving it automatically would save typing on
a table that gets typed regardless.

Hand-write the table. Do not build the demangler for this.

---

## The option that removes the problem entirely

**Build the host 32-bit and `LoadLibrary("msvcp60.dll")`.**

MSVCP60 is a real, redistributable 32-bit DLL that still ships and still loads
on Windows 11. A 32-bit host can load it, `GetProcAddress` each of the 82
exports, and call them directly. That is not a shim -- it is the actual
implementation the game was compiled against, with bit-exact semantics,
including `basic_string`'s reference-counted copy-on-write layout, which any
reimplementation would have to match byte-for-byte because the game's own code
reads those fields directly.

58 shims become 0.

It is not free, and the trade is worth stating precisely:

| | 64-bit host (today) | 32-bit host |
|---|---|---|
| MSVCP60 | 58 hand-written `__thiscall` shims | load the real DLL |
| MSVCRT | 67 shims (written, working) | load the real DLL |
| Handles | HWND/HDC/FILE\* are 64-bit, so every one crosses a translation table | natively 32-bit, tables disappear |
| `PAINTSTRUCT` and friends | different size from the game's, field-by-field copy | identical layout, pass the pointer |
| Pointers | host `malloc` is outside the 32-bit space, so a custom allocator hands out VAs | host `malloc` *is* a valid target pointer |
| Image mapping | 1:1 at 0x00400000, host based high (0x140000000) | 1:1 still possible, host based at e.g. 0x30000000 |
| Address space | effectively unlimited | 2 GB, and the game's own data is ~0.9 GB of `.rpk` |
| Toolchain | what the collection uses | `-m32`, and `runtime/recomp32` has never been built that way |

The last two rows are the real cost. Everything above them is a saving, and
several of those savings are of the "works until it does not" kind -- the handle
table in `shims_impl.c` already carries a comment saying so, and the
`PAINTSTRUCT` field copy is the same class of problem.

`runtime/recomp32/image_loader.c` says the host must be linked high "so the
target's VA range is free and the mapping is 1:1 (g_mem_base stays 0)", and
notes that on a 64-bit host pointers are formed through `(uintptr_t)` casts. A
32-bit host wants exactly the same property for a different reason, and gets it
by basing the host above the target's 0x00400000..0x10000000 span rather than
outside 32 bits altogether.

## Recommendation

Try the 32-bit host before writing 58 STL shims. The experiment is cheap: build
the existing host with `-m32` and a host base of 0x30000000, and see whether the
splash still renders. If it does, the whole MSVCRT/MSVCP60 problem collapses
into `LoadLibrary` + `GetProcAddress`, and the handle tables and struct
marshalling go with it.

If it does not, the 58 shims are still there to write, and nothing is lost but
an afternoon.

**Status: not tried.** The splash screen renders on the 64-bit host, and the
function catalog -- needed before any wider closure can be lifted -- is still
being built.
