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

Try the 32-bit host before writing 58 STL shims. If it works the whole
MSVCRT/MSVCP60 problem collapses into `LoadLibrary` + `GetProcAddress`, and the
handle tables and struct marshalling go with it. If it does not, the 58 shims
are still there to write and nothing is lost.

### Which compiler

Checked, because this decides whether the experiment is cheap:

| | |
|---|---|
| `mingw64` + `-m32` | **no.** Exits 1 with no output -- the 32-bit runtime is not installed, and `/c/msys64/mingw32/bin` has no gcc in it. |
| MSVC x86 cross | **yes.** `VC/Tools/MSVC/14.44.35207/bin/Hostx64/x86/cl.exe` is present. |

So the pivot means switching the host from gcc to `cl` as well as from 64- to
32-bit, which is a bigger step than a flag. The `CMakeLists.txt` already carries
an MSVC branch with the right warning suppressions, and `/BASE` replaces the
`-Wl,--image-base` used for the 64-bit host -- but 2.5 million lines of
generated C have only ever been through gcc, and MSVC's tolerance for a single
1.2 MB function-dense translation unit is unmeasured.

Budget it as a day, not an afternoon, and measure the compile on one chunk
before converting the build.

## Where it actually stops today

The whole binary is lifted (38,908 functions) and runs into the game's own
startup: the CRT entry completes, `_initterm` runs **358 static constructors**,
`CoInitialize`, `QueryPerformanceFrequency`, `GetVersionExA` and
`GetStartupInfoA` all go through, and hundreds of lifted functions execute.

Then it faults in `sub_0053DDC0`, and the reason is this document: every one of
the 58 STL entry points now has a correct purge count and a **stub body**, so
the stack stays balanced and every `basic_string` is uninitialised garbage. The
trace before the fault is exactly what that predicts --
`basic_string(const char*, const allocator&)`, `assign`, `_Grow`, `_Copy`,
`find`, `find_last_of`, `_Xlen` -- and then a dereference of a string that was
never constructed.

That is the gate, reached and confirmed rather than reasoned about.


---

# The pattern, after eleven blockers

Renamed in spirit: this is no longer about the STL. Every blocker since has had
the same shape, and the shape is the finding.

| # | Blocker | Why it blocked |
|---|---|---|
| 1 | `basic_string` layout | `allocator<char>` occupies a padded first slot; fields are 4 bytes later than the library source suggests |
| 2 | 13 STL operators | free functions backref `std::`, so the mangled name differs from the member spelling |
| 3 | `GetStartupInfoA` | a stub that must FILL IN a struct left the CRT reading stack garbage |
| 4 | `_adjust_fdiv` | a data import self-patched to its own address reads as "this CPU has the FDIV bug" |
| 5 | `npos` | same, and it is 0xFFFFFFFF or string code stops working |
| 6 | `LoadLibrary`/`GetProcAddress` | returning 1 means the game calls through 1 |
| 7 | `CoCreateInstance` | S_OK without writing the out pointer is a guaranteed null dereference |
| 8 | `QueryInterface` | accepting every IID puts surface methods at Direct3D slots |
| 9 | `DirectDrawEnumerateA` vs `ExA` | 2 args vs 3, 4-arg callback vs 5; sharing a shim is a wrong purge |
| 10 | enumeration callbacks | returning DD_OK without calling back leaves an empty device list |
| 11 | `GetCaps` | `dwVidMemTotal = 0` is a device no surface can be allocated on; and the field is at 0x3C, not 0x54 |

Not one of these was a defect in the lifted code. **Every one was an
infidelity in a hand-written reimplementation of a Windows or DirectX API.**
38,908 lifted functions have not produced a single confirmed lifter bug in this
phase; the whole cost has been in the shim layer.

That changes the recommendation from "worth trying" to "this is the wrong
architecture for this binary".

## Why the 32-bit host is now the answer

Every DLL the game wants is a real, present, 32-bit DLL:

| | |
|---|---|
| `msvcp60.dll`, `msvcrt.dll` | ship with Windows; still load |
| `ddraw.dll`, `dinput.dll` | ship with Windows 11 |
| `mss32.dll`, `smush.dll` | **on the game's own disc**, already in `game/` |

A 32-bit host can `LoadLibrary` all of them and the game's COM calls reach the
real implementations. Items 1, 2, 7, 8, 9, 10 and 11 above simply cease to
exist -- they are all "my DirectDraw/STL is not the real one".

### The objection, and why it is smaller than it looks

The recomp model keeps a simulated stack separate from the host C stack, so
calling a real native function means marshalling arguments -- and for an
arbitrary function pointer the count is unknown.

For **cdecl** it is not needed. A cdecl callee pops nothing, so copying a fixed
generous number of slots (16, say) from the simulated stack to the real stack
and calling works without knowing the count: the callee reads only what it
wants, and the cleanup is the caller's, which is us. That covers all 149
MSVCRT/MSVCP60 imports in one thunk.

For **stdcall and thiscall** the count still matters, but that is the table
`stdcall_argc.py` already derives -- 248 of 307 from evidence on the machine --
and `ddraw.lib` is in the Windows SDK, so `DirectDrawCreate` resolves to 3
slots today. COM vtable methods are fixed per interface and are the table
already written in `ddraw_shims.c`.

So the marshalling work is real but bounded, and it is *one* thunk plus a table
that exists, versus faithfully reimplementing DirectDraw, Direct3D, DirectInput,
Miles, SMUSH and the MSVC 6 STL.

### Cost, stated honestly

- MSVC x86 (`Hostx64/x86/cl.exe`) is present; mingw has no 32-bit runtime here.
- 2.5 million lines of generated C have only been through gcc. MSVC's appetite
  for a function-dense 1.2 MB translation unit is unmeasured -- time one chunk
  before converting the build.
- 2 GB of address space, against a 274 MB `.rpk` the game memory-maps.
- `runtime/recomp32` has never been built 32-bit.

## What does NOT change

~~The `.rpk` reader still has to be written either way.~~ **Done** -- see the
`tools/rpk.py` commit. The directory is decoded and the 9,554 members tile the
274 MB archive with no gap and no overlap, so the format is no longer a
question. It was never going to be the long pole it looks like: the game reads
its own archive with its own lifted code, so what a host owes it is working file
I/O, not a reimplementation. The reader earns its place as the thing that says
whether the bytes the game gets are the bytes that are there.

The decision above still stands on its own terms, and the evidence for it has
only got stronger: of everything that blocked this session -- `_fullpath`,
`_splitpath`, `sscanf`'s `%n` and its EOF return, `GetCurrentDirectoryA`
returning a relative path, `WIN32_FIND_DATAA` leaking host stack contents into
target memory, `CreateThread`'s argument index -- not one was a defect in the
lifted code. Every one was an infidelity in a hand-written reimplementation of
a Windows or CRT API. That is now sixteen for sixteen.

The one thing a 32-bit host would NOT fix is the other finding of this session:
`CreateThread` cannot be honoured while the machine state is a single set of
globals, whatever word size the host is. That is orthogonal, and it is the next
structural decision. See docs/STARTUP.md.
