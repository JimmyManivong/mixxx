#!/usr/bin/env python3
"""Rekordbox-style Phrase / Music-Structure Analysis for Mixxx.
Pipeline: madmom RNN beat/downbeat → librosa spectral features →
          SSM + Foote novelty boundaries → bar-grid snap → heuristic labels.
"""
import argparse
import hashlib
import json
import os
import sys
import numpy as np

SR = 22050
HOP = 512

DEFAULT_CACHE_DIR = os.path.expanduser(
    "~/Library/Containers/org.mixxx.mixxx/Data/Library/"
    "Application Support/Mixxx/phrase_cache"
)


def cache_key(path):
    return hashlib.sha1(os.path.abspath(path).encode("utf-8")).hexdigest()


def track_beats_madmom(path):
    """RNN-based beat + downbeat tracking. Returns (bpm, beat_times_s, downbeat_offset)."""
    from madmom.features.beats import RNNBeatProcessor, DBNBeatTrackingProcessor
    from madmom.features.downbeats import RNNDownBeatProcessor, DBNDownBeatTrackingProcessor

    beat_act = RNNBeatProcessor()(path)
    beats = DBNBeatTrackingProcessor(fps=100)(beat_act)  # seconds array

    dbeat_act = RNNDownBeatProcessor()(path)
    dbeat_info = DBNDownBeatTrackingProcessor(beats_per_bar=[4], fps=100)(dbeat_act)
    # [[beat_time, beat_pos], ...] where beat_pos 1 = downbeat

    bpm = 60.0 / float(np.median(np.diff(beats))) if len(beats) > 1 else 120.0

    off = 0
    if len(dbeat_info) > 0 and len(beats) > 0:
        downbeat_times = dbeat_info[dbeat_info[:, 1] == 1, 0]
        if len(downbeat_times) > 0:
            off = int(np.argmin(np.abs(beats - downbeat_times[0])))

    return float(bpm), np.array(beats, dtype=float), int(off)


def beat_sync_features(path, beat_times):
    """Beat-synchronized spectral features via librosa."""
    import librosa

    y, sr = librosa.load(path, sr=SR, mono=True)
    total_dur = float(len(y) / sr)

    beat_frames = librosa.time_to_frames(beat_times, sr=SR, hop_length=HOP)
    beat_frames = np.clip(beat_frames, 0, len(y) // HOP - 1)

    S = np.abs(librosa.stft(y, hop_length=HOP))
    freqs = librosa.fft_frequencies(sr=sr)
    rms = librosa.feature.rms(y=y, hop_length=HOP)[0]
    low_energy = S[freqs < 200.0, :].mean(axis=0)
    high_energy = S[freqs >= 200.0, :].mean(axis=0)
    mfcc = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=13, hop_length=HOP)
    chroma = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=HOP)

    def sync(feat):
        return librosa.util.sync(feat, beat_frames, aggregate=np.mean)

    rms_b = sync(rms[np.newaxis, :])[0]
    low_b = sync(low_energy[np.newaxis, :])[0]
    high_b = sync(high_energy[np.newaxis, :])[0]
    mfcc_b = sync(mfcc)
    chroma_b = sync(chroma)

    return rms_b, low_b, high_b, mfcc_b, chroma_b, total_dur


def _norm(arr):
    a = np.array(arr, dtype=float)
    mn, mx = a.min(), a.max()
    return (a - mn) / (mx - mn + 1e-9)


def build_ssm(mfcc_b, chroma_b):
    """Self-similarity matrix weighted MFCC (timbre) + chroma (harmony)."""
    def norm_cols(a):
        n = np.linalg.norm(a, axis=0, keepdims=True)
        n[n < 1e-9] = 1.0
        return a / n

    feat = np.vstack([norm_cols(mfcc_b) * 2.0, norm_cols(chroma_b)])
    feat = norm_cols(feat)
    ssm = feat.T @ feat
    np.fill_diagonal(ssm, 1.0)
    return ssm


def foote_novelty(ssm, kernel_size=32):
    """Gaussian-tapered Foote checkerboard novelty curve."""
    n = ssm.shape[0]
    h = min(kernel_size, n // 4)
    if h < 2:
        return np.zeros(n)

    g = np.exp(-0.5 * (np.arange(h) / (h / 2.5)) ** 2)
    kernel = np.outer(g, g)
    cb = np.block([[kernel, -kernel], [-kernel, kernel]])

    nov = np.zeros(n)
    for i in range(h, n - h):
        nov[i] = float(np.sum(ssm[i - h:i + h, i - h:i + h] * cb))

    if nov.max() > nov.min():
        nov = (nov - nov.min()) / (nov.max() - nov.min())
    return nov


def foote_novelty_multiscale(ssm):
    """Combine k=16 (fine) and k=32 (coarse) novelty curves."""
    n16 = foote_novelty(ssm, kernel_size=16)
    n32 = foote_novelty(ssm, kernel_size=32)
    combined = np.maximum(n16, n32)
    if combined.max() > combined.min():
        combined = (combined - combined.min()) / (combined.max() - combined.min())
    return combined


def detect_boundaries(novelty, beats, off, bpm):
    """Peak-pick novelty, snap to 4-bar grid, return sorted list of beat indices."""
    from scipy.signal import find_peaks

    n = len(beats)
    min_dist = max(8, int(4 * 4 * 0.6))  # ~3-4 bars minimum between boundaries

    peaks, _ = find_peaks(novelty, distance=min_dist, prominence=0.02, height=0.04)

    chosen = {off}
    for p in peaks:
        rel = int(p) - off
        snapped = off + round(rel / 4) * 4  # snap to 1-bar (4-beat) grid
        if 0 <= snapped < n:
            chosen.add(snapped)

    return sorted(chosen)


def label_segments(boundaries, beats, rms_b, low_b, high_b, total_dur):
    """Heuristic label assignment: INTRO / CHORUS / DOWN / UP / OUTRO."""
    n = len(beats)
    ext = list(boundaries) + [n]

    segs = []
    for i in range(len(boundaries)):
        s, e = boundaries[i], ext[i + 1]
        sl = slice(s, min(e, n))
        segs.append({
            "start": round(float(beats[min(s, n - 1)]), 3),
            "end": round(float(beats[min(e, n - 1)]) if e < n else total_dur, 3),
            "rms": float(np.mean(rms_b[sl])),
            "low": float(np.mean(low_b[sl])),
            "high": float(np.mean(high_b[sl])),
            "label": "",
        })

    if not segs:
        return [{"start": 0.0, "end": round(total_dur, 3), "label": "TRACK"}]
    segs[-1]["end"] = round(total_dur, 3)

    n_rms = _norm([s["rms"] for s in segs])
    n_low = _norm([s["low"] for s in segs])
    n_high = _norm([s["high"] for s in segs])

    high_thresh = np.percentile(n_high, 40)
    low_thresh = np.percentile(n_low, 30)

    for i, seg in enumerate(segs):
        if n_low[i] < low_thresh:
            seg["label"] = "DOWN"
        elif n_high[i] > high_thresh:
            seg["label"] = "CHORUS"
        else:
            seg["label"] = "BEATS"

    # INTRO: leading non-CHORUS sections
    for seg in segs:
        if seg["label"] in ("BEATS", "DOWN"):
            seg["label"] = "INTRO"
        else:
            break

    # OUTRO: after last CHORUS, with energy-based refinement
    last_chorus = max((i for i, s in enumerate(segs) if s["label"] == "CHORUS"), default=-1)
    if last_chorus >= 0:
        peak_h = n_high.max()
        for i in range(last_chorus, -1, -1):
            if segs[i]["label"] == "CHORUS" and n_high[i] < peak_h * 0.45:
                segs[i]["label"] = "OUTRO"
            else:
                break
        last_chorus = max((i for i, s in enumerate(segs) if s["label"] == "CHORUS"), default=-1)
        for i in range(last_chorus + 1, len(segs)):
            segs[i]["label"] = "OUTRO"

    # Remaining BEATS → DOWN
    for seg in segs:
        if seg["label"] == "BEATS":
            seg["label"] = "DOWN"

    # UP: last DOWN(s) immediately before CHORUS
    for i in range(len(segs) - 1):
        if segs[i]["label"] == "DOWN" and segs[i + 1]["label"] == "CHORUS":
            segs[i]["label"] = "UP"
            if i > 0 and segs[i - 1]["label"] == "DOWN" and n_high[i] > n_high[i - 1]:
                segs[i - 1]["label"] = "UP"

    # Smooth single-section anomalies (2 passes)
    for _ in range(2):
        for i in range(1, len(segs) - 1):
            if (segs[i - 1]["label"] == segs[i + 1]["label"]
                    and segs[i]["label"] not in ("UP",)
                    and segs[i - 1]["label"] not in ("UP",)):
                segs[i]["label"] = segs[i - 1]["label"]

    # Build output and merge consecutive same labels
    out = [{"start": s["start"], "end": s["end"], "label": s["label"]} for s in segs]
    merged = [out[0]]
    for s in out[1:]:
        if s["label"] == merged[-1]["label"]:
            merged[-1]["end"] = s["end"]
        else:
            merged.append(s)

    # Number repeated labels
    totals = {}
    for s in merged:
        totals[s["label"]] = totals.get(s["label"], 0) + 1
    seen = {}
    for s in merged:
        lbl = s["label"]
        if totals.get(lbl, 0) > 1:
            seen[lbl] = seen.get(lbl, 0) + 1
            s["label"] = f"{lbl} {seen[lbl]}"

    return merged


def force_outro_split(boundaries, beats, high_b, bpm):
    """Scan the last 38% of the track for the biggest high-freq drop (melody off
    = OUTRO start). Excludes the final 12 bars to avoid triggering on fade-out."""
    n = len(beats)
    beats_per_bar = 4
    # Search window: 62% of track → (n - 12 bars)
    start_search = int(n * 0.62)
    end_search = n - beats_per_bar * 12

    if start_search >= end_search:
        return boundaries

    win = beats_per_bar * 8
    best_beat, best_drop = -1, 0.0

    for b in range(start_search, end_search):
        e_before = float(np.mean(high_b[max(0, b - win // 2):b]))
        e_after  = float(np.mean(high_b[b:min(n, b + win // 2)]))
        ref = max(e_before, 1e-9)
        drop = (e_before - e_after) / ref
        if drop > best_drop:
            best_drop, best_beat = drop, b

    if best_drop > 0.15 and best_beat > 0:
        off = boundaries[0] if boundaries else 0
        snapped = off + round((best_beat - off) / beats_per_bar) * beats_per_bar
        # Only insert if outro would be at least 8 bars
        if snapped not in boundaries and 0 <= snapped < n and n - snapped >= beats_per_bar * 8:
            boundaries = sorted(set(boundaries) | {snapped})

    return boundaries


def analyze(path):
    bpm, beats, off = track_beats_madmom(path)

    if len(beats) < 16:
        import librosa
        y, sr = librosa.load(path, sr=SR, mono=True)
        dur = float(len(y) / sr)
        return {
            "version": 1, "duration": round(dur, 3), "bpm": round(bpm, 2),
            "segments": [{"start": 0.0, "end": round(dur, 3), "label": "TRACK"}],
        }

    rms_b, low_b, high_b, mfcc_b, chroma_b, total_dur = beat_sync_features(path, beats)
    ssm = build_ssm(mfcc_b, chroma_b)
    novelty = foote_novelty_multiscale(ssm)
    boundaries = detect_boundaries(novelty, beats, off, bpm)
    boundaries = force_outro_split(boundaries, beats, high_b, bpm)
    segs = label_segments(boundaries, beats, rms_b, low_b, high_b, total_dur)

    return {
        "version": 1,
        "duration": round(total_dur, 3),
        "bpm": round(bpm, 2),
        "downbeat_beat_offset": int(off),
        "segments": segs,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio")
    ap.add_argument("--out")
    ap.add_argument("--cache-dir", default=DEFAULT_CACHE_DIR)
    ap.add_argument("--print", action="store_true")
    args = ap.parse_args()

    result = analyze(args.audio)

    out = args.out
    if not out:
        os.makedirs(args.cache_dir, exist_ok=True)
        out = os.path.join(args.cache_dir, cache_key(args.audio) + ".json")
    with open(out, "w") as f:
        json.dump(result, f, indent=2)

    if args.print or args.out is None:
        print(f"# {os.path.basename(args.audio)}")
        print(f"# bpm={result['bpm']} dur={result['duration']}s -> {out}")
        for s in result["segments"]:
            print(f"  {s['start']:7.1f}s  {s['label']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
