"""Analyse it_test BIN (NPC smoke/heat grid) vs VDB (visual) data. Read-only."""
import glob
import os
import re
import struct
import sys

import numpy as np

ROOT = r"C:\CodexWork\FireData_it_test"
BINS = {1: "smoke_data_it_test_1.bin", 2: "smoke_data_it_test_2.bin", 3: "smoke_data_it_test_3_v1.bin"}
THRESH = 10  # same default as AYUFSBinaryManager debug threshold (0..255)


def vdb_meta(path):
    data = open(path, "rb").read()
    out = {"size": len(data)}
    for key, fmt, n in (("file_bbox_min", "<3i", 12), ("file_bbox_max", "<3i", 12), ("file_voxel_count", "<q", 8)):
        i = data.find(key.encode())
        if i < 0:
            continue
        j = i + len(key)
        # metadata record: name, then type string (len-prefixed), then value size + value
        m = re.match(rb"(....)(vec3i|int64)(....)", data[j:j + 20], re.S)
        if not m:
            continue
        k = j + m.end()
        out[key] = struct.unpack(fmt, data[k:k + n])
        if len(out[key]) == 1:
            out[key] = out[key][0]
    grids = sorted(set(g.decode() for g in re.findall(rb"\x00\x00\x00([a-z_]{3,20})\x00", data[:4096])))
    out["names"] = [g for g in grids if g in ("density", "temperature", "flame", "heat", "fuel", "smoke", "soot")]
    m = re.search(rb"(UniformScaleTranslateMap|ScaleTranslateMap|UniformScaleMap|ScaleMap)", data[:8192])
    if m:
        k = m.end()
        vals = struct.unpack("<9d", data[k:k + 72]) if len(data) > k + 72 else ()
        out["map"] = m.group(1).decode()
        out["map_vals"] = tuple(round(v, 4) for v in vals)
    return out


def bin_stats(path):
    with open(path, "rb") as f:
        frames, dx, dy, dz = struct.unpack("<4i", f.read(16))
    grid = dx * dy * dz
    mm = np.memmap(path, dtype=np.uint8, mode="r", offset=16, shape=(frames, 2, dx, dy, dz))
    rows = []
    for fr in range(frames):
        den = mm[fr, 0]
        tmp = mm[fr, 1]
        d = den >= THRESH
        t = tmp >= THRESH
        rows.append((fr, int(den.max()), int(tmp.max()), int(d.sum()), int(t.sum())))
    return (frames, dx, dy, dz), rows, mm


def bbox(mask):
    idx = np.argwhere(mask)
    if idx.size == 0:
        return None
    return tuple(idx.min(0).tolist()), tuple(idx.max(0).tolist())


def main():
    for n, name in BINS.items():
        path = os.path.join(ROOT, name)
        (frames, dx, dy, dz), rows, mm = bin_stats(path)
        print(f"\n===== fire {n}: {name}  frames={frames} dims=({dx},{dy},{dz}) size_ok={os.path.getsize(path) == 16 + 2 * dx * dy * dz * frames}")
        onset_d = next((r[0] for r in rows if r[3] > 0), None)
        onset_t = next((r[0] for r in rows if r[4] > 0), None)
        peak = max(rows, key=lambda r: r[3])
        print(f"smoke onset frame={onset_d} heat onset frame={onset_t} peak smoke voxels={peak[3]} at frame {peak[0]} "
              f"maxDensity={max(r[1] for r in rows)} maxTemp={max(r[2] for r in rows)}")
        for fr in (0, 10, 50, 100, 200, 400, 600, 800, frames - 1):
            r = rows[fr]
            b = bbox(mm[fr, 0] >= THRESH)
            hb = bbox(mm[fr, 1] >= THRESH)
            print(f"  f{fr:4d} maxD={r[1]:3d} maxT={r[2]:3d} smokeVox={r[3]:6d} heatVox={r[4]:5d} smokeBBox={b} heatBBox={hb}")
        # where the fire starts: hottest cell in first heat frame
        if onset_t is not None:
            t = np.asarray(mm[onset_t, 1])
            print(f"  ignition (hottest cell at heat onset) = {np.unravel_index(t.argmax(), t.shape)}")
        # smoke vertical profile at peak: fraction of smoke voxels per z layer
        prof = (np.asarray(mm[peak[0], 0]) >= THRESH).sum(axis=(0, 1))
        print(f"  z-profile at peak (smoke voxels per z): {prof.tolist()}")

        vdbs = sorted(glob.glob(os.path.join(ROOT, f"vdb_{n}", "*.vdb")))
        print(f"  VDB files={len(vdbs)} first={os.path.basename(vdbs[0])} last={os.path.basename(vdbs[-1])}")
        sizes = np.array([os.path.getsize(p) for p in vdbs], dtype=float)
        empty = int((sizes < sizes.min() * 1.05 + 1).sum())
        print(f"  VDB near-empty frames={empty} first non-trivial={int(np.argmax(sizes > sizes.min() * 1.5)) + 1}")
        for fr in (1, 11, 51, 101, 201, 401, 601, 801, len(vdbs)):
            meta = vdb_meta(vdbs[min(fr, len(vdbs)) - 1])
            print(f"  vdb#{fr:4d} {meta}")
        # time alignment: correlate BIN smoke voxel count with VDB file size
        cnt = np.array([r[3] for r in rows], dtype=float)
        m = min(len(cnt), len(sizes))
        best = max(range(-20, 21), key=lambda s: np.corrcoef(cnt[max(0, s):m + min(0, s)], sizes[max(0, -s):m - max(0, s)])[0, 1])
        corr0 = np.corrcoef(cnt[:m], sizes[:m])[0, 1]
        print(f"  time correlation BIN smoke volume vs VDB size: offset0={corr0:.3f} best offset={best}")
        sys.stdout.flush()


main()
