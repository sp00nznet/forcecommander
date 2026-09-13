#!/usr/bin/env python3
"""Check every COM vtable shim's purge count against the DirectX signature.

A __stdcall method pops its own arguments, so a shim's STDRET(n) has to equal
1 + the number of arguments the real method takes. Get it wrong and the target
stack drifts by the difference -- and the failure does not land in the shim, it
lands wherever the caller next pops a saved register.

That has now happened twice in this project. IDirectDraw7::SetDisplayMode takes
FIVE arguments where IDirectDraw v1 took three; purging for three left eight
bytes behind, and CUtilityDevice::SelectRenderer returned from creating the
screen with `this` == 0 and faulted writing this+0x48. Nothing in the fault
pointed at DirectDraw.

So the argument counts live here, taken from the DirectX 7 headers, and the
check reads them back out of the shim source:

    py -3 tools/com_purge_check.py

ponytail: the tables are hand-transcribed from the headers, which is the only
place they exist. A wrong number here is a wrong number, but a wrong number
here is one line to fix instead of a week to find.
"""
import os
import re
import sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '..', 'src', 'runtime', 'ddraw_shims.c')

# method name -> argument count, NOT counting `this`. In vtable order, but the
# check keys on the name in the shim table so order does not have to match.
IDIRECTDRAW7 = {
    'QueryInterface': 2, 'AddRef': 0, 'Release': 0,
    'Compact': 0, 'CreateClipper': 3, 'CreatePalette': 4, 'CreateSurface': 3,
    'DuplicateSurface': 2, 'EnumDisplayModes': 4, 'EnumSurfaces': 4,
    'FlipToGDISurface': 0, 'GetCaps': 2, 'GetDisplayMode': 1,
    'GetFourCCCodes': 2, 'GetGDISurface': 1, 'GetMonitorFrequency': 1,
    'GetScanLine': 1, 'GetVerticalBlankStatus': 1, 'Initialize': 1,
    'RestoreDisplayMode': 0, 'SetCooperativeLevel': 2, 'SetDisplayMode': 5,
    'WaitForVerticalBlank': 2, 'GetAvailableVidMem': 3, 'GetSurfaceFromDC': 2,
    'RestoreAllSurfaces': 0, 'TestCooperativeLevel': 0,
    'GetDeviceIdentifier': 2, 'StartModeTest': 3, 'EvaluateMode': 2,
}

IDIRECT3DDEVICE7 = {
    'QueryInterface': 2, 'AddRef': 0, 'Release': 0,
    'GetCaps': 1, 'EnumTextureFormats': 2, 'BeginScene': 0, 'EndScene': 0,
    'GetDirect3D': 1, 'SetRenderTarget': 2, 'GetRenderTarget': 1, 'Clear': 6,
    'SetTransform': 2, 'GetTransform': 2, 'SetViewport': 1,
    'MultiplyTransform': 2, 'GetViewport': 1, 'SetMaterial': 1,
    'GetMaterial': 1, 'SetLight': 2, 'GetLight': 2, 'SetRenderState': 2,
    'GetRenderState': 2, 'BeginStateBlock': 0, 'EndStateBlock': 1,
    'PreLoad': 1, 'DrawPrimitive': 5, 'DrawIndexedPrimitive': 7,
    'SetClipStatus': 1, 'GetClipStatus': 1, 'DrawPrimitiveStrided': 5,
    'DrawIndexedPrimitiveStrided': 7, 'DrawPrimitiveVB': 5,
    'DrawIndexedPrimitiveVB': 7, 'ComputeSphereVisibility': 4,
    'GetTexture': 2, 'SetTexture': 2, 'GetTextureStageState': 3,
    'SetTextureStageState': 3, 'ValidateDevice': 1, 'ApplyStateBlock': 1,
    'CaptureStateBlock': 1, 'DeleteStateBlock': 1, 'CreateStateBlock': 2,
    'Load': 5, 'LightEnable': 2, 'GetLightEnable': 2, 'SetClipPlane': 2,
    'GetClipPlane': 2, 'GetInfo': 3,
}

IDIRECT3D7 = {
    'QueryInterface': 2, 'AddRef': 0, 'Release': 0,
    'EnumDevices': 2, 'CreateDevice': 3, 'CreateVertexBuffer': 3,
    'EnumZBufferFormats': 3, 'EvictManagedTextures': 0,
}

IFACES = {'IDirectDraw': IDIRECTDRAW7, 'IDirectDraw2': IDIRECTDRAW7,
          'IDirectDraw4': IDIRECTDRAW7, 'IDirectDraw7': IDIRECTDRAW7,
          'IDirect3D7': IDIRECT3D7, 'IDirect3DDevice7': IDIRECT3DDEVICE7}


def purge_counts(src):
    """shim function name -> the set of STDRET counts in its body."""
    out = {}
    for m in re.finditer(r'static void (\w+)\(void\) \{', src):
        name = m.group(1)
        # the body, to the next top-level `static void` or a comment banner
        rest = src[m.end():]
        nxt = re.search(r'\nstatic void \w+\(void\) \{|\n/\* -----', rest)
        body = rest[:nxt.start()] if nxt else rest
        n = set(int(x) for x in re.findall(r'STDRET\((\d+)\)', body))
        if n:
            out[name] = n
    return out


def main():
    src = open(SRC, encoding='utf-8').read()
    purge = purge_counts(src)

    bad = unknown = checked = 0
    for fn, label in re.findall(r'\{\s*(\w+),\s*"([^"]+)"\s*\}', src):
        if '::' not in label:
            continue
        iface, method = label.split('::', 1)
        table = IFACES.get(iface)
        if table is None:
            continue
        want = table.get(method)
        if want is None:
            print('  no signature for %s::%s' % (iface, method))
            unknown += 1
            continue
        got = purge.get(fn)
        if got is None:
            continue                    # a one-liner the parser skipped
        checked += 1
        if want + 1 not in got:
            print('  %-44s %-22s STDRET%s, want STDRET(%d)'
                  % (label, fn, sorted(got), want + 1))
            bad += 1

    print('%d entries checked, %d wrong, %d without a signature'
          % (checked, bad, unknown))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
