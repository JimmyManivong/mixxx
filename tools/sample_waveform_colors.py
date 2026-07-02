#!/usr/bin/env python3
"""Sample the dominant Rekordbox/CDJ waveform colours from a screen capture.

Feed it a clean capture of a Rekordbox/CDJ-3000 waveform and it prints the
measured low (bass / blue), mid (orange) and high (white) RGB values, ready to
paste into res/skins/LateNight/skin.xml (SignalRGBLowColor / Mid / High).

Usage:
    python3 tools/sample_waveform_colors.py capture1.png [capture2.jpg ...]
    # optional: restrict the vertical band that contains the big waveform
    python3 tools/sample_waveform_colors.py --band 42 95 capture.png

Image decoding: uses Pillow if available, otherwise falls back to macOS `sips`
(converts to a temporary 24-bit BMP, no extra dependency).
"""
import argparse
import struct
import subprocess
import sys
import tempfile
import os
import numpy as np


def _load_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    off = struct.unpack_from('<I', data, 10)[0]
    w = struct.unpack_from('<i', data, 18)[0]
    h = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise ValueError(f"need a 24-bit BMP, got {bpp}bpp")
    rb = (w * 3 + 3) // 4 * 4
    H = abs(h)
    a = np.empty((H, w, 3), np.uint8)
    for y in range(H):
        row = np.frombuffer(data, np.uint8, w * 3, off + y * rb).reshape(w, 3)
        a[H - 1 - y if h > 0 else y] = row[:, ::-1]  # BGR(bottom-up) -> RGB
    return a


def load_image(path):
    """Return an HxWx3 uint8 RGB array, via Pillow or a sips->BMP fallback."""
    try:
        from PIL import Image
        return np.asarray(Image.open(path).convert('RGB'))
    except ImportError:
        pass
    # macOS fallback
    tmp = tempfile.NamedTemporaryFile(suffix='.bmp', delete=False)
    tmp.close()
    try:
        subprocess.run(['sips', '-s', 'format', 'bmp', path, '--out', tmp.name],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return _load_bmp(tmp.name)
    finally:
        os.unlink(tmp.name)


def _report(label, px):
    if len(px) < 10:
        print(f"  {label:16s}: n={len(px)} (trop peu de pixels)")
        return
    med = np.median(px, axis=0).astype(int)
    print(f"  {label:16s}: n={len(px):5d}   #{med[0]:02X}{med[1]:02X}{med[2]:02X}"
          f"   ({med[0]},{med[1]},{med[2]})")


def sample(path, band):
    img = load_image(path)
    H, W, _ = img.shape
    y0, y1 = band if band else (int(H * 0.26), int(H * 0.59))
    region = img[y0:y1, 8:W - 8].reshape(-1, 3).astype(int)
    r, g, b = region[:, 0], region[:, 1], region[:, 2]
    print(f"\n=== {path}  ({W}x{H}, bande y={y0}..{y1}) ===")
    _report("LOW  (bass/blue)", region[(b > 170) & (r < 60) & (g < 145)])
    _report("MID  (orange)",    region[(r > 175) & (b < 90) & (g > 70) & (g < 175)])
    _report("HIGH (white)",     region[region.min(1) > 165])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('images', nargs='+')
    ap.add_argument('--band', nargs=2, type=int, metavar=('Y0', 'Y1'),
                    help="vertical pixel range of the main waveform (default: middle ~26-59%%)")
    args = ap.parse_args()
    for path in args.images:
        try:
            sample(path, args.band)
        except Exception as e:  # noqa: BLE001
            print(f"!! {path}: {e}", file=sys.stderr)


if __name__ == '__main__':
    main()
