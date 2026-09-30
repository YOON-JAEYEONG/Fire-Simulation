"""Find the interior doorways (room <-> corridor/room) of the Main building from the wall meshes.

Line traces do not work in a commandlet world, so, like survey_main_exits.py, this slices the
wall meshes' triangles with horizontal planes. A doorway is a gap that is open at 30/100/180 cm
above the floor on BOTH faces of a wall, 60-260 cm wide, with wall again above it (a lintel).
Open halls (no lintel) and windows (closed at 30 cm) are rejected.

Output: Saved/main_room_door_survey.txt, one line per doorway:
  floor axis wall_mid wall_thickness lo hi floor_z head_height
Run headless:  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
"""
import os
import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "main_room_door_survey.txt")
# Building shell (cm): outer faces x=0..4300, y=-1840..0. Exterior walls are handled by the exits.
X0, X1, Y0, Y1 = 0.0, 4300.0, -1840.0, 0.0
OUTER_BAND = 45.0
FLOORS = (("1F", ("\ubcbd_0", "Model"), 0.0), ("2F", ("\ubcbd_3500", "Model"), 350.0))   # wall mesh labels, floor z
DOOR_HEIGHTS = (30.0, 100.0, 180.0)
LINTEL_PROBES = [200.0 + 5.0 * i for i in range(17)]            # 200..280 cm above the floor
MIN_W, MAX_W = 60.0, 260.0
FACE_PAIR = 45.0                                                  # max wall thickness
STEP = 2.0

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/Maps/Main")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lines = []


def triangles(actor):
    smc = actor.get_component_by_class(unreal.StaticMeshComponent)
    xf = smc.get_world_transform()
    desc = smc.static_mesh.get_static_mesh_description(0)
    out = []
    for t in range(desc.get_triangle_count()):
        tid = unreal.TriangleID(t) if hasattr(unreal, "TriangleID") else t
        vids = desc.get_triangle_vertices(tid)
        pts = [xf.transform_location(desc.get_vertex_position(v)) for v in vids]
        out.append(pts[-3:])  # the API returns 3 default IDs ahead of the real ones
    return out


def slice_z(tri, z):
    pts = []
    for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
        if (a.z - z) * (b.z - z) < 0:
            t = (z - a.z) / (b.z - a.z)
            pts.append((a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)))
    return pts if len(pts) == 2 else None


def faces_at(tris, z):
    """{('y', coord): [(lo, hi)...], ('x', coord): [...]} for axis-aligned wall faces at height z."""
    faces = {}
    for tri in tris:
        seg = slice_z(tri, z)
        if not seg:
            continue
        (ax, ay), (bx, by) = seg
        if abs(ay - by) < 1.0 and abs(ax - bx) > 1.0:        # face runs along X at y=const
            key, lo, hi = ("y", round((ay + by) / 2.0)), min(ax, bx), max(ax, bx)
        elif abs(ax - bx) < 1.0 and abs(ay - by) > 1.0:      # face runs along Y at x=const
            key, lo, hi = ("x", round((ax + bx) / 2.0)), min(ay, by), max(ay, by)
        else:
            continue
        faces.setdefault(key, []).append((lo, hi))
    return faces


def covered(intervals, v):
    return any(lo - 0.5 <= v <= hi + 0.5 for lo, hi in intervals)


def near(faces, key, tol=2):
    """intervals of every face within tol cm of key (faces are keyed by rounded coordinate)."""
    axis, c = key
    out = []
    for d in range(-tol, tol + 1):
        out += faces.get((axis, c + d), [])
    return out


def is_exterior(axis, c):
    if axis == "y":
        return c > Y1 - OUTER_BAND or c < Y0 + OUTER_BAND
    return c < X0 + OUTER_BAND or c > X1 - OUTER_BAND


for floor, prefixes, fz in FLOORS:
    wall_actors = [a for a in eas.get_all_level_actors()
                   if any(a.get_actor_label().startswith(p) for p in prefixes)
                   and a.get_component_by_class(unreal.StaticMeshComponent)]
    if not wall_actors:
        lines.append("# missing wall actors " + ",".join(prefixes))
        continue
    tris = [t for a in wall_actors for t in triangles(a)]
    by_h = {h: faces_at(tris, fz + h) for h in DOOR_HEIGHTS}
    lintel = {h: faces_at(tris, fz + h) for h in LINTEL_PROBES}
    ref = by_h[100.0]
    found = []
    for key, segs in ref.items():
        axis, c = key
        if is_exterior(axis, c) or sum(hi - lo for lo, hi in segs) < 150.0:
            continue
        lo_all, hi_all = min(s[0] for s in segs), max(s[1] for s in segs)
        # Walk along the face; a gap is where no face covers the point at any door height.
        v, start = lo_all, None
        while v <= hi_all + STEP:
            open_all = all(not covered(near(by_h[h], key), v) for h in DOOR_HEIGHTS)
            inside = lo_all <= v <= hi_all
            if open_all and inside and start is None:
                start = v
            if (not open_all or not inside) and start is not None:
                found.append((axis, c, start, v))
                start = None
            v += STEP
    # Keep gaps with the right width, a partner face (the other side of the wall) and a lintel.
    if os.environ.get("YUFS_DOOR_SURVEY_DEBUG"):
        for key in sorted(ref):
            segs = ref[key]
            lines.append("#   face %s=%d len=%.0f span=%.0f..%.0f" % (key[0], key[1], sum(h - l for l, h in segs),
                                                                   min(s[0] for s in segs), max(s[1] for s in segs)))
        for axis, c, lo, hi in found:
            lines.append("#   gap %s=%d %.0f..%.0f w=%.0f" % (axis, c, lo, hi, hi - lo))
    doors = []
    for axis, c, lo, hi in found:
        width = hi - lo
        if width < MIN_W or width > MAX_W:
            continue
        mid = (lo + hi) / 2.0
        partners = [(a, cc, l, h) for a, cc, l, h in found
                    if a == axis and 3 <= abs(cc - c) <= FACE_PAIR and abs((l + h) / 2.0 - mid) < 20.0]
        if not partners:
            continue
        other = min(partners, key=lambda p: abs(p[1] - c))
        head = None
        for h in LINTEL_PROBES:
            if covered(near(lintel[h], (axis, c)), mid) and covered(near(lintel[h], (axis, other[1])), mid):
                head = h
                break
        if head is None:
            continue       # no wall above: an open hall, not a doorway
        wall_mid = (c + other[1]) / 2.0
        lo2, hi2 = max(lo, other[2]), min(hi, other[3])
        doors.append((axis, round(wall_mid, 1), abs(c - other[1]), round(lo2, 1), round(hi2, 1), head))
    # Each doorway was found from both faces: keep one.
    unique = []
    for d in sorted(doors):
        if not any(u[0] == d[0] and abs(u[1] - d[1]) < 5 and abs(u[3] - d[3]) < 10 for u in unique):
            unique.append(d)
    lines.append("# %s: %d wall faces at 100 cm, %d raw gaps, %d doorways" % (floor, len(ref), len(found), len(unique)))
    for axis, wall_mid, thick, lo, hi, head in unique:
        lines.append("%s %s %.1f %.1f %.1f %.1f %.1f %.1f" % (floor, axis, wall_mid, thick, lo, hi, fz, head))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
unreal.log("[YUFSDoorSurvey] wrote %s (%d lines)" % (OUT, len(lines)))
