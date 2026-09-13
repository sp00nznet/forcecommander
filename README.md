# Star Wars: Force Commander — Static Recompilation

Static recompilation of **Star Wars: Force Commander** (LucasArts, 2000) from
its shipping Win32 binary to native C.

Built on the [pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain, and
next to [xwa](https://github.com/sp00nznet/xwa) — same publisher, same year,
same studio's tooling.

## Project Status: **P0 complete, P1 not started**

---

## What P0 found

| Binary | Size | `.text` | Built | Role |
|--------|-----:|--------:|-------|------|
| `Focom.exe` | 4,943,872 | **3,936,882** | 2000-03-03 | the game |
| `Force.exe` | 106,496 | — | 2000-02-29 | launcher |
| `Smush.dll` | 147,456 | — | 1999-02-27 | LucasArts SMUSH video (4 exports) |
| `mss32.dll` | 328,704 | — | 1999-01-04 | Miles Sound System |
| `FocomSetup.dll` | 843,776 | — | 2000-02-29 | installer |

MSVC 6.0. Imports `MSVCP60.dll` — the C++ standard library, so this is
STL-heavy C++, not the plain C of the mid-90s shelf.

**No DRM.** Entropy 5.31–6.45 across every section, no wrapper sections, no
packer. Nothing to dump, nothing to unwrap.

Two things about that are worth stating plainly:

**3.94 MB of `.text` makes this the second-largest target in the collection.**
Only Rise of Legends (13.25 MB, 19.2 million instructions) is bigger, and Rise
of Legends is explicitly the stress test rather than a project. Force Commander
is roughly 1.5× X-Wing Alliance and 4× Crimson Skies — real scale, but scale
the toolchain has already handled at 6,232 functions on Crimson Skies.

**It is the clean version of the X-Wing Alliance problem.** XWA is the project
that produced most of the 32-bit pipeline and it needed
`drm/safedisc_dump.py` and a custom memory dumper before a single byte could be
read, because static unwrapping was not viable. Same publisher, one year later,
no protection at all. Everything the XWA work learned about LucasArts binaries
applies, and the first week of it does not have to happen again.

There is no `.reloc` section — fixed image base, no ASLR, which is what
`runtime/recomp32/image_loader.c` already assumes.

`Smush.dll` is the one piece that does not need reverse engineering. SMUSH is
LucasArts' video codec and ScummVM has implemented it for two decades; that is
a read, not a recovery.

## Where it goes next (P1)

1. `pe/pe_analyze.py` then `disasm/disasm32.py` over `Focom.exe`. Expect a long
   run — 3.9 MB with C++ tail calls is exactly the shape that found the E9
   seeding bug on Trespasser, so seed `jmp rel32` from the start.
2. **Score before lifting.** There is no linker map and no PDB here, so the
   reference has to come from IDA (`tools/ida/ida_funcs.py`). At this size,
   lifting an unscored catalog is 400,000 lines of maybe.
3. `tools/cpp/` for the MSVC mangling — MSVCP60 means STL templates in the
   symbol soup, which is a different demangling workload from Black & White's
   hand-written hierarchy.
4. Disc 2 has not been unpacked yet.

## Layout

```
forcecommander/
  original/   both discs (.bin/.cue, and fc1.iso converted from disc 1)
  game/       extracted binaries
  analysis/   P0 output
  docs/
```

## Credits

Star Wars: Force Commander © 2000 LucasArts Entertainment Company. This project
neither contains nor distributes any part of it.
