"""Find openings in the Main building's OUTER walls from the wall mesh geometry.

Line traces do not work in a commandlet world, and the saved navmesh never reaches the
outer faces, so this slices the wall meshes' triangles at door height (z=100 cm on
floor 1) and reports gaps along each outer face. Output: Saved/main_exit_survey.txt
"""
import os
import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "main_exit_survey.txt")
X0, X1, Y0, Y1 = 0.0, 4300.0, -1840.0, 0.0
BAND = 60.0      # how far inside an outer face a wall triangle may sit
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/Maps/Main")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lines = []


def triangles(actor):
    smc = actor.get_component_by_class(unreal.StaticMeshComponent)
    mesh = smc.static_mesh
    xf = smc.get_world_transform()
    desc = mesh.get_static_mesh_description(0)
    out = []
    for t in range(desc.get_triangle_count()):
        tid = unreal.TriangleID(t) if hasattr(unreal, "TriangleID") else t
        vids = desc.get_triangle_vertices(tid)
        pts = [xf.transform_location(desc.get_vertex_position(v)) for v in vids]
        out.append(pts[-3:])  # the API returns 3 default IDs ahead of the real ones
    return out


def slice_z(tri, z):
    """segment where triangle crosses plane z (or None)."""
    pts = []
    for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
        if (a.z - z) * (b.z - z) < 0:
            t = (z - a.z) / (b.z - a.z)
            pts.append((a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)))
    return pts if len(pts) == 2 else None


def gaps(cover, lo, hi, step=10):
    res, start = [], None
    x = lo
    while x <= hi:
        covered = any(a - 1 <= x <= b + 1 for a, b in cover)
        if not covered and start is None:
            start = x
        if covered and start is not None:
            res.append((start, x))
            start = None
        x += step
    if start is not None:
        res.append((start, hi))
    return [g for g in res if g[1] - g[0] >= 60]


for label_prefix, zs in (("벽_0", (30.0, 100.0, 180.0)), ("Model", (30.0, 100.0, 180.0)), ("벽_3500", (380.0, 450.0, 530.0))):
    actor = next((a for a in eas.get_all_level_actors() if a.get_actor_label().startswith(label_prefix)), None)
    if not actor:
        lines.append("missing actor " + label_prefix)
        continue
    tris = triangles(actor)
    allp = [p for t in tris for p in t]
    lines.append("== %s triangles=%d  x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f sample=%s" % (
        label_prefix, len(tris), min(p.x for p in allp), max(p.x for p in allp), min(p.y for p in allp),
        max(p.y for p in allp), min(p.z for p in allp), max(p.z for p in allp), tris[0]))
    for z in zs:
        faces = {"south y=-1840": [], "north y=0": [], "west x=0": [], "east x=4300": []}
        for tri in tris:
            seg = slice_z(tri, z)
            if not seg:
                continue
            (ax, ay), (bx, by) = seg
            if min(ay, by) < Y0 + BAND:
                faces["south y=-1840"].append((min(ax, bx), max(ax, bx)))
            if max(ay, by) > Y1 - BAND:
                faces["north y=0"].append((min(ax, bx), max(ax, bx)))
            if min(ax, bx) < X0 + BAND:
                faces["west x=0"].append((min(ay, by), max(ay, by)))
            if max(ax, bx) > X1 - BAND:
                faces["east x=4300"].append((min(ay, by), max(ay, by)))
        for name, cover in faces.items():
            lo, hi = (X0, X1) if name[0] in "sn" else (Y0, Y1)
            lines.append("  z=%.0f %s segs=%d gaps=%s" % (z, name, len(cover), gaps(cover, lo, hi)))
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
unreal.log("[YUFSExitSurvey] done")
