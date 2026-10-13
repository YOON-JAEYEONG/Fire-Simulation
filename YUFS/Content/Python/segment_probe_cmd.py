"""Commandlet: path connectivity down the west/east upper runs, pair by pair (editor-world navmesh)."""
import os, unreal
OUT = os.path.join(unreal.Paths.project_saved_dir(), "stair_segments_cmd.txt")
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/Main")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
L = []
def proj(p, ext=(6, 6, 80)):
    r = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(*p), None, None, unreal.Vector(*ext))
    if isinstance(r, tuple): r = r[0]
    return None if (r is None or (r.x == 0 and r.y == 0 and r.z == 0)) else r
def path_len(a, b):
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, a, b, None, None)
    if p is None or not p.is_valid(): return "INVALID"
    pts = list(p.path_points)
    return "%s %.0f" % ("partial" if p.is_partial() else "ok", sum(unreal.Vector.distance(pts[i], pts[i+1]) for i in range(len(pts)-1)))
for name, dx, xs in (("WEST", 0, (240, 260, 270, 280, 300)), ("EAST", 3920, (4160, 4180, 4190, 4200, 4220))):
    L.append("# %s: projected nav point per (x,y) [extent 6,6,80 around the nose-line height], then path from the 2F point (x,-282,355)" % name)
    for x in xs:
        top = proj((x, -282, 355), (6, 6, 30))
        L.append("x=%d top=%s" % (x, ("(%.0f,%.0f,%.0f)" % (top.x, top.y, top.z)) if top else None))
        prev = top
        for y in range(-298, -560, -16):
            z = 192 + (y + 521) * 0.6245 if y > -521 else 192
            q = proj((x, y, z))
            if q is None:
                L.append("   y=%d z~%.0f: no nav" % (y, z)); continue
            s_top = path_len(top, q) if top else "n/a"
            s_prev = path_len(prev, q) if prev else "n/a"
            L.append("   y=%d z~%.0f: nav=(%.0f,%.0f,%.0f) fromTop=%s fromPrev=%s" % (y, z, q.x, q.y, q.z, s_top, s_prev))
            prev = q
open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
