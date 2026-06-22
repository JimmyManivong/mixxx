#!/usr/bin/env python3
"""Import Rekordbox PWV7 waveforms from a Pioneer USB drive into Mixxx.

Pipeline:
  USB .2EX (PWV7, 150 samples/sec, 3-byte entries: bass/mid/high 0-63)
  → scale to 0-255
  → Mixxx protobuf Waveform + WaveformSummary
  → qCompress (4-byte big-endian header + zlib)
  → Mixxx analysis/{id} files
  → Update mixxxdb.sqlite track_analysis table

Usage:
  python3 rekordbox_waveform_import.py --usb /Volumes/JIMMY [--dry-run]
"""
import argparse
import os
import sqlite3
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, '/tmp')  # waveform_pb2.py generated there
import waveform_pb2

MIXXX_SUPPORT = os.path.expanduser(
    "~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx"
)
MIXXX_DB = os.path.join(MIXXX_SUPPORT, "mixxxdb.sqlite")
MIXXX_ANALYSIS_DIR = os.path.join(MIXXX_SUPPORT, "analysis")

RB_WAVEFORM_RATE = 150.0   # Rekordbox PWV7: 150 samples/sec
RB_PWV7_MAX = 127.0        # PWV7 values are 7-bit (0-127)
WAVEFORM_DESCRIPTION = "Waveform 5.0"
WAVEFORM_VERSION = "Waveform-5.0"
WAVEFORM_SUMMARY_DESCRIPTION = "WaveformSummary 5.0"
WAVEFORM_SUMMARY_VERSION = "WaveformSummary-5.0"
WAVEFORM_TYPE = 1
WAVEFORM_SUMMARY_TYPE = 2


def qcompress(data: bytes) -> bytes:
    """Qt qCompress: 4-byte big-endian original size + zlib."""
    compressed = zlib.compress(data, level=6)
    return struct.pack(">I", len(data)) + compressed


def qt_qchecksum(data: bytes) -> int:
    """Qt 5 qChecksum: CRC-16/IBM-SDLC (poly=0x8408, init=0xFFFF, xorout=0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        c = b
        for _ in range(8):
            if (crc ^ c) & 0x01:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
            c >>= 1
    return (~crc) & 0xFFFF


def make_signal(values_lr: np.ndarray, max_val: int = 255) -> waveform_pb2.Waveform.Signal:
    """Build a Waveform.Signal from a flat L/R interleaved uint8 array."""
    sig = waveform_pb2.Waveform.Signal()
    sig.value.extend(values_lr.astype(np.int32).tolist())
    sig.channels = 2
    sig.units = waveform_pb2.Waveform.AMPLITUDE
    sig.max_value = max_val
    sig.min_value = 0
    return sig


def pwv7_to_mixxx_waveform(pwv7_data: np.ndarray, duration_s: float) -> bytes:
    """
    Convert PWV7 (N×3 uint8 array, values 0-63, rate=150/s) to
    Mixxx Waveform protobuf + qCompress.

    pwv7_data: shape (N, 3) — columns are [bass, mid, high]
    """
    n = len(pwv7_data)

    # Scale 0-127 → 0-255
    bass  = (pwv7_data[:, 0].astype(np.float32) * (255.0 / RB_PWV7_MAX)).clip(0, 255).astype(np.uint8)
    mid   = (pwv7_data[:, 1].astype(np.float32) * (255.0 / RB_PWV7_MAX)).clip(0, 255).astype(np.uint8)
    high  = (pwv7_data[:, 2].astype(np.float32) * (255.0 / RB_PWV7_MAX)).clip(0, 255).astype(np.uint8)
    all_  = np.maximum(np.maximum(bass, mid), high)

    # Interleave L/R (Mixxx expects 2 channels; duplicate since RB is mono)
    def lr(arr):
        out = np.empty(n * 2, dtype=np.uint8)
        out[0::2] = arr
        out[1::2] = arr
        return out

    wf = waveform_pb2.Waveform()
    wf.visual_sample_rate = RB_WAVEFORM_RATE
    wf.audio_visual_ratio = 1.0  # we work directly at waveform sample rate

    wf.signal_all.CopyFrom(make_signal(lr(all_)))

    wf.signal_filtered.low.CopyFrom(make_signal(lr(bass)))
    wf.signal_filtered.mid.CopyFrom(make_signal(lr(mid)))
    wf.signal_filtered.high.CopyFrom(make_signal(lr(high)))

    return qcompress(wf.SerializeToString())


def pwv7_to_mixxx_summary(pwv7_data: np.ndarray, summary_len: int = 1000) -> bytes:
    """
    Build a WaveformSummary (overview waveform) by downsampling PWV7 to ~1000 points.
    """
    n = len(pwv7_data)
    factor = max(1, n // summary_len)

    # Downsample by taking max in each block
    trimmed = pwv7_data[:n - n % factor].reshape(-1, factor, 3)
    ds = trimmed.max(axis=1)  # (summary_len, 3)

    bass  = (ds[:, 0].astype(np.float32) * (255.0 / RB_PWV7_MAX)).clip(0, 255).astype(np.uint8)
    mid   = (ds[:, 1].astype(np.float32) * (255.0 / RB_PWV7_MAX)).clip(0, 255).astype(np.uint8)
    high  = (ds[:, 2].astype(np.float32) * (255.0 / RB_PWV7_MAX)).clip(0, 255).astype(np.uint8)
    all_  = np.maximum(np.maximum(bass, mid), high)

    m = len(bass)
    rate = RB_WAVEFORM_RATE / factor

    def lr(arr):
        out = np.empty(m * 2, dtype=np.uint8)
        out[0::2] = arr
        out[1::2] = arr
        return out

    wf = waveform_pb2.Waveform()
    wf.visual_sample_rate = rate
    wf.audio_visual_ratio = 1.0

    wf.signal_all.CopyFrom(make_signal(lr(all_)))
    wf.signal_filtered.low.CopyFrom(make_signal(lr(bass)))
    wf.signal_filtered.mid.CopyFrom(make_signal(lr(mid)))
    wf.signal_filtered.high.CopyFrom(make_signal(lr(high)))

    return qcompress(wf.SerializeToString())


def read_pwv7(path_2ex: str):
    """Return (N, 3) uint8 array from a .2EX file, or None if no PWV7."""
    from pyrekordbox import AnlzFile
    anlz = AnlzFile.parse_file(path_2ex)
    pwv7 = anlz.get_tag('PWV7')
    if pwv7 is None:
        return None
    c = pwv7.content
    data = np.frombuffer(c.entries, dtype=np.uint8)
    n = c.len_entries
    return data[:n * c.len_entry_bytes].reshape(n, c.len_entry_bytes)


def build_usb_index(usb_root: str):
    """
    Scan PIONEER/USBANLZ on USB, return dict:
      filename (lowercase) → (local_path_on_usb, path_to_2ex)
    Uses PPTH tag to get the original filename.
    """
    from pyrekordbox import AnlzFile
    usbanlz = os.path.join(usb_root, 'PIONEER', 'USBANLZ')
    index = {}  # filename_lower → (ppth_path, path_2ex)

    for dirpath, dirs, files in os.walk(usbanlz):
        if 'ANLZ0000.2EX' in files:
            path_2ex = os.path.join(dirpath, 'ANLZ0000.2EX')
            try:
                anlz = AnlzFile.parse_file(path_2ex)
                ppth = anlz.get_tag('PPTH')
                if ppth:
                    fname = os.path.basename(ppth.path).lower()
                    index[fname] = (ppth.path, path_2ex)
            except Exception:
                pass

    print(f"  Indexed {len(index)} tracks on USB")
    return index


def get_mixxx_tracks(db_path: str):
    """Return list of (library_id, file_path) from Mixxx DB."""
    conn = sqlite3.connect(db_path)
    rows = conn.execute(
        "SELECT l.id, tl.location "
        "FROM library l JOIN track_locations tl ON l.location = tl.id "
        "WHERE tl.location IS NOT NULL AND l.mixxx_deleted = 0"
    ).fetchall()
    conn.close()
    return rows


def get_existing_analysis(conn, track_id: int):
    """Return dict of {type: (analysis_id, data_checksum)} for a track."""
    rows = conn.execute(
        "SELECT id, type, data_checksum FROM track_analysis WHERE track_id = ?",
        (track_id,)
    ).fetchall()
    return {r[1]: (r[0], r[2]) for r in rows}


def write_analysis(conn, track_id: int, analysis_type: int,
                   description: str, version: str,
                   data: bytes, analysis_dir: str, dry_run: bool):
    """Write analysis file and update DB. data must be the raw qCompress bytes."""
    checksum = qt_qchecksum(data)

    existing = get_existing_analysis(conn, track_id)
    if analysis_type in existing:
        analysis_id, old_checksum = existing[analysis_type]
        if int(old_checksum) == checksum:
            return False  # unchanged
        if not dry_run:
            conn.execute(
                "UPDATE track_analysis SET description=?, version=?, "
                "data_checksum=?, created=CURRENT_TIMESTAMP "
                "WHERE id=?",
                (description, version, checksum, analysis_id)
            )
            with open(os.path.join(analysis_dir, str(analysis_id)), 'wb') as f:
                f.write(data)
        return True
    else:
        if not dry_run:
            cur = conn.execute(
                "INSERT INTO track_analysis (track_id, type, description, version, data_checksum) "
                "VALUES (?, ?, ?, ?, ?)",
                (track_id, analysis_type, description, version, checksum)
            )
            analysis_id = cur.lastrowid
            with open(os.path.join(analysis_dir, str(analysis_id)), 'wb') as f:
                f.write(data)
        return True


def main():
    ap = argparse.ArgumentParser(description="Import Rekordbox waveforms into Mixxx")
    ap.add_argument("--usb", default="/Volumes/JIMMY", help="USB drive mount point")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--limit", type=int, default=0, help="Process only N tracks (0=all)")
    args = ap.parse_args()

    if not os.path.exists(args.usb):
        print(f"USB not found at {args.usb}")
        sys.exit(1)

    print(f"Building USB index from {args.usb} ...")
    usb_index = build_usb_index(args.usb)
    if not usb_index:
        print("No tracks found on USB.")
        sys.exit(1)

    print(f"Loading Mixxx library from {MIXXX_DB} ...")
    mixxx_tracks = get_mixxx_tracks(MIXXX_DB)
    print(f"  {len(mixxx_tracks)} tracks in Mixxx library")

    conn = sqlite3.connect(MIXXX_DB)
    converted = skipped = not_found = errors = 0

    for i, (track_id, location) in enumerate(mixxx_tracks):
        if args.limit and i >= args.limit:
            break

        fname = os.path.basename(location).lower()
        if fname not in usb_index:
            not_found += 1
            continue

        ppth_path, path_2ex = usb_index[fname]

        try:
            pwv7 = read_pwv7(path_2ex)
            if pwv7 is None or len(pwv7) < 100:
                skipped += 1
                continue

            duration_s = len(pwv7) / RB_WAVEFORM_RATE
            wf_data  = pwv7_to_mixxx_waveform(pwv7, duration_s)
            sum_data = pwv7_to_mixxx_summary(pwv7)

            changed_wf  = write_analysis(conn, track_id, WAVEFORM_TYPE,
                                          WAVEFORM_DESCRIPTION, WAVEFORM_VERSION,
                                          wf_data, MIXXX_ANALYSIS_DIR, args.dry_run)
            changed_sum = write_analysis(conn, track_id, WAVEFORM_SUMMARY_TYPE,
                                          WAVEFORM_SUMMARY_DESCRIPTION, WAVEFORM_SUMMARY_VERSION,
                                          sum_data, MIXXX_ANALYSIS_DIR, args.dry_run)

            if changed_wf or changed_sum:
                converted += 1
                print(f"  ✓ {os.path.basename(location)[:60]}")
            else:
                skipped += 1

        except Exception as e:
            errors += 1
            print(f"  ✗ {os.path.basename(location)[:50]}: {e}")

    if not args.dry_run:
        conn.commit()
    conn.close()

    print(f"\nDone: {converted} converted, {skipped} skipped (up-to-date), "
          f"{not_found} not on USB, {errors} errors")
    if args.dry_run:
        print("(dry-run — nothing written)")


if __name__ == "__main__":
    main()
