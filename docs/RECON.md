# What is inside Focom.exe

P1 reconnaissance. Everything here came out of the shipped binary and the
retail disc; nothing was guessed.

---

## The engine has a name, and it is not Force Commander

The binary's own RTTI names 567 classes across four namespaces:

| namespace | classes | what it is |
|---|---:|---|
| `RE3D` | 71 | the renderer |
| `Ronin` | 61 | the base framework — refcounting, factories, loaders |
| `GEAPI` | 52 | scene graph and geometry |
| `DX7` | 24 | DirectX 7 helpers and instrumentation |

plus `Subsystem`, `GamePPMultiplayer`, `GamePPVisBase` and friends.

The surviving `__FILE__` strings agree: every one is a path under
`C:\ronin\gamepp\gameppmultiplayer\`. **Ronin** is the engine and **GamePP**
is the game framework on top of it. Force Commander is one title built on
them, which is why the class names talk about screens and factories rather
than about AT-ATs.

This is a full C++ class graph recovered from a stripped retail binary with
`pcrecomp`'s `tools/cpp/rtti.py`: **1,121 vtables, 5,436 virtual methods, and
4,832 of them attributable to exactly one class.**

## The renderer is behind an interface, and that is the whole opportunity

`RE3D` is not "the Direct3D code". It is an abstraction with at least three
implementations sitting behind it:

```
CUtilityDriver / CUtilityScreen / CUtilityRenderer / CUtilitySurfaceAny
        |
        +-- CDD7StdDriver, CDD7MMDriver        DirectDraw 7 drivers
        +-- CDD7FSScreen / CDD7FSSurface       full screen
        +-- CDD7WinScreen / CDD7WinSurface     windowed
        +-- CDD7MemRenderer                    renders into memory
        +-- CD3D7Renderer (107 methods)        hardware, Direct3D 7
        +-- CD3D7GeometryRenderer (94)
```

Two things follow.

**There is a software path.** `CDD7MemRenderer` renders into a memory surface
and `CDD7WinScreen`/`CDD7WinSurface` present it in a window. A bring-up does
not have to begin by implementing Direct3D 7 — it has to hand the game a
lockable surface and blit whatever the game writes into it.

**The seam is already drawn.** This is the same shape as Nocturne's 37-call
`APIDLL*` renderer boundary: the game talks to `CProtoScreen`-style interfaces,
not to DirectDraw directly, so the replacement point is an interface the
original authors defined.

Note what the imports do *not* say. `DDRAW.DLL` appears nowhere in the import
table — it is loaded with `LoadLibrary`, along with `SMUSH.DLL` and
`FEELIT.DLL`. The static import list names 307 functions from 11 DLLs and
none of them are DirectX. Reading the IAT alone would have missed the entire
graphics stack.

The GUIDs in `.rdata` settle the versions: DirectDraw 2, 4 and 7, Direct3D 2
and 3, DirectInput 1 and 2.

## Formats

### `.znm` movies are gzipped SMUSH

`Resource\Movies\*.znm` begin `1f 8b 08 08` — gzip, with the original filename
preserved in the header as `faraway.snm`. Decompressed, they are `SANM`
containers:

```
SANM
  SHDR   version 1, 225 frames, 640x480
  FLHD -> Bl16
  ANNO
  FRME x225
```

Each `FRME` holds one `Bl16` chunk: 16-bit colour, 640x480, with a per-frame
header giving `seq` and `codec`. Frame 0 is `seq=0 codec=5` (intra); every
frame after is `codec=2` (motion compensated). This is LucasArts SMUSH, which
ScummVM has implemented for two decades — a read, not a recovery.

`CSmushMemRenderCallback` in the RTTI confirms the video decodes into the same
memory surface the rest of the renderer uses.

### `.rpk` is a "Ronin PakFile"

`forcecommand.rpk` is 274 MB and holds essentially the whole game.

```
+0x00  "Ronin PakFile\r\n\0"
+0x10  u32  offset of the directory (0x1051ED24)
+0x14  CRLF-separated member names, then member data back to back
       (the first member is a3de001.wav, and RIFF/WAVE data
        begins immediately after the name list at 0x3B53B)
...
0x1051ED24  directory: 18 bytes of 0xFF, u32 0, u16 0,
            u32 count = 7000, then (u32 id, u32 len, char[len])
            string records, followed by fixed-size entries
            carrying timestamps
```

The string table is the project's whole vocabulary, and the first entries are
the asset categories: `Subsystems`, `RE3D`, `Textures`, `Models`,
`Animations`, `Voice Lines`, `Sound Effects`, `Fonts`,
`Front End Screens`, `Screen Stages`. The record layout past the strings is
mapped but not finished.

### `.M3D` is not a model

`Resource\MSSA3D.M3D`, `MSSEAX.M3D`, `MSSFAST.M3D` and friends are **Miles
Sound System drivers**, and `MSSB16.TSK` is a Miles task file. The `.3dm` /
`.3da` extensions in the binary's strings are the model and animation formats;
`.M3D` on the disc is audio. Easy to get backwards, so it is written down.

## The first thing that renders

`Focom.exe` carries one `BITMAP` resource: 640x480, 8bpp, 308,264 bytes —
307,200 pixels plus a 40-byte header and a 1,024-byte palette. It is the
loading screen, two Rebel T4-B tanks firing over grassland.

Extracting it needed a PE resource reader, which `pcrecomp` did not have; it
is now `tools/pe/rsrc.py` there. The catch is that a `BITMAP` resource has no
`BITMAPFILEHEADER` — the loader knows where the pixels start, so the file does
not say — and rebuilding it requires the palette size, which the header
reports as 0 meaning "all of them".

It is not reproduced here. A screenshot of *our* renderer drawing it would be
our own output; the resource itself is LucasArts' artwork, and extracting it
for analysis is not the same as redistributing it. Run `tools/pe/rsrc.py` on
your own copy.

## No DRM, and a fixed base

Entropy 5.31–6.45 across all four sections, no wrapper, no packer, no
`.reloc`. Image base 0x00400000, entry at 0x0056EB66. MSVC 6.0, and
`MSVCP60.dll` with 82 imports — this is STL-heavy C++, not the plain C of the
mid-90s shelf.
