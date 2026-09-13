# What Force Commander does before it draws anything

Recovered by running it. Every step here was a blocker, in this order.

## Focom.ini is a script, not a settings file

The game reads `<installdir>\Focom.ini` and will not start without it. It is not
an INI -- there are no `GetPrivateProfile*` imports at all. It is a list of
directives, one per line, read with `sscanf(line, "%4095s %n")` and dispatched
on the keyword against the game's own table at 0x0083E5E4:

    Wait  Run  Options  Players  GameFiles  Music  Movies  Workspace
    RPKDir  RPKFile  InitBase  SetRegKey  HideLoadingPanel  ShowLoadingPanel
    LoadAppFileName  CheckCD  CheckAppMutex

The disc ships `Install\Focom.ini` as **zero bytes** -- the installer writes the
real one at install time with paths substituted. So a copy lifted off the disc
is empty, nothing parses, and the game dies dereferencing a member that
`InitBase` would have filled.

The template is the installer's own, read out of the compiled InstallShield
script `Install\setup.ins` (its string literals are plain text in the bytecode;
search for `CheckAppMutex`). `tools/make_focom_ini.py` writes it for a given
install directory. `RPKDir` really is emitted twice.

## The install layout is not the disc layout

`Resource\*` on the disc is copied into `<installdir>\Resource\`, and every path
in the script is absolute and goes through `Resource\`:

    <installdir>\Focom.ini
    <installdir>\Focom.exe
    <installdir>\Resource\appname.ini
    <installdir>\Resource\forcecommand\forcecommand.{rpk,gpl}
    <installdir>\Resource\forcecommand\project.gam
    <installdir>\Resource\{Movies,Music,GameFiles,Players}\

`GameFiles` and `Players` are not on the disc; the installer creates them empty.

## Settings live in the registry, not on disk

Under `LucasArts Entertainment Company LLC\Force Commander\v1.0`, with
`Settings\Screen` (`UseLowQualityTextures`) and `Settings\Game`
(`UseMipmapping`, `LODLevel`, `FogLevel`, `Brightness`). The game creates the
key itself and writes its own defaults, so the only thing a host has to do is
let `Reg*` through to a real registry -- there is nothing to invent. The error
string on the failure path is its own: "Failed to create file system registry
object!".

## Paths must come back absolute

The game calls `GetCurrentDirectoryA` / `GetModuleFileNameA` and feeds the
result straight into `_fullpath`. A shim answering with a relative `"game"` gets
it resolved a second time, and every path the game derives is then one directory
too deep -- it looked for `game\game\Focom.ini`, did not find it, and exited 0
without a word.

## The sequence, as it runs

    CheckAppMutex FORCE        CreateMutexA; a NULL handle reads as
                               "another instance is already running"
    CheckCD <installdir>       GetVolumeInformationA; disc 1 is labelled FOCOM_1
    LoadAppFileName ...        reads Resource\appname.ini
    ShowLoadingPanel           CreateDialogParamA(101), whose dialog procedure
                               is the game's own lifted code at 0x00401770 --
                               the same one that draws the splash. It paints.
    InitBase                   builds GamePPProdBase, creates the registry key,
                               and spawns a service thread
    RPKDir / Workspace / ...   mount points for the .rpk and the .gpl path map
    Run 2 1 6
    HideLoadingPanel

## Where it stops today

The game boots, loads, and starts its first mission. It then exits cleanly.

`Run 2 1 6` is `GamePPProdStartup` slot 19, forwarding to `GamePPProdBase`
slot 61 (`sub_0051CE70`), which ends by calling slot 14 on the object at
`GamePPProdBase + 0x94`. That object is the **GamePPVisProcessManager**
(vtable 0x007C58B4), and slot 14 is `sub_0052B410` -- start a process.

**The three numbers are an object template ID.** `.pro` files key `id` and
`inheritID` as triples, and template `2 1 6` is `Trasse - Day/info.pro`: the
opening mission. So the last live line of Focom.ini means "boot the Trasse
mission", and it works -- the lookup scans the compiled template database,
finds it, allocates, and registers a process.

What then happens, per `--calltrace` with thread tags:

| | |
|---|---:|
| main thread calls | 7,356,408 |
| service thread calls | 6 (parked in its wait loop, correct) |
| **mission process calls** | **590,206** |
| object templates compiled | 1,713 |
| `.rpk` reads | 288,585 |
| heap high-water | 200 MB over 692,799 allocations |

The mission process is an OS thread. Its routine runs the mission's boot script
to completion and returns; `sub_005510F0` then signals the process's completion
event, the manager reaps the process, the live-process count
(`sub_0052B6B0`, which counts non-null entries in a 16-slot array at
`[mgr+0xC]`) drops to zero, the main thread's loop in `sub_005003CD` ends, and
WinMain returns. `exit(0)` comes from `0x0056EB66` -- the CRT's
`exit(WinMain(...))`. Nothing is wrong with the shutdown; the game finished
what it was asked to do.

So the question is why the mission's script terminates instead of looping, and
the honest answer is that it is not yet known. Two things are established:

- **It is not the frame tick.** `sub_0051D480` -- the base's slot 62, which the
  loop calls with elapsed seconds -- runs exactly once, between the count
  returning 1 and returning 0. The loop is willing to keep ticking; there is
  nothing left to tick.
- **It is not a wall-clock timeout.** Ronin's queue-consumer body
  (`sub_005518C0`) is `WaitForSingleObject(mutex, 100)` and returns on timeout,
  which looked like an obvious candidate given that this machine serialises
  lifted code. `--waitscale 100` changes nothing, so that is not it.

What is suspicious is that **no Direct3D device is ever created.** The game
enumerates one and accepts it -- `sub_0076ACC0` compares the offered
`deviceGUID` against four it knows (TnLHal, HAL, RGB, Ref; RGB is offered and
matches, landing in device slot `+0x44C`) -- then releases IDirect3D7 without
calling `CreateDevice`, and never calls `SetDisplayMode`, `Lock`, `Blt` or
`Flip`. A mission script whose first act is to set up rendering would explain
both the absence of drawing and a script that ends early. That is the thread to
pull next.

### A correction

An earlier version of this document said `GamePPProdBase + 0x94` was null at
`Run` and named that as the cause. It is not null. That conclusion came from
`--poison` on an address captured in a *different run*, and allocation order
varies once the game's own threads are running. Read in a single run with
`--watch`, the slot holds a live GamePPVisProcessManager. Poison and watch
addresses are only comparable within one process.
