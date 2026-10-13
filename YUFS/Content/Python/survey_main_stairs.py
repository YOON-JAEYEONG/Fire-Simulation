"""Read-only: slice the stair meshes and the 2F wall/floor mesh to learn the stair runs and landings."""
import os, unreal
OUT = os.path.join(unreal.Paths.project_saved_dir(), "main_stair_survey.txt")
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/Main")
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
        out.append(pts[-3:])
    return out
actors = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
for label in ("\uacc4\ub2e8_x846", "\uacc4\ub2e8_x1618", "\uadf8\ub8f9"):
    a = actors.get(label)
    if not a: lines.append("# missing " + label); continue
    tris = triangles(a)
    lines.append("# %s: %d tris" % (label, len(tris)))
    # horizontal (walkable) faces: normal mostly up; print each distinct z with its xy extent
    flat = {}
    for tri in tris:
        zs = [p.z for p in tri]
        if max(zs) - min(zs) < 1.0:
            z = round(zs[0])
            xs = [p.x for p in tri]; ys = [p.y for p in tri]
            lo = flat.setdefault(z, [1e9, 1e9, -1e9, -1e9])
            lo[0] = min(lo[0], *xs); lo[1] = min(lo[1], *ys); lo[2] = max(lo[2], *xs); lo[3] = max(lo[3], *ys)
    for z in sorted(flat):
        b = flat[z]
        lines.append("  flat z=%d x=%.0f..%.0f y=%.0f..%.0f" % (z, b[0], b[2], b[1], b[3]))
# 2F mesh: horizontal faces at z ~ 350 (floor) and their extents; find holes by listing faces at 335..365
for label in ("\ubcbd_3500_mm", "Model", "\ubc14\ub2e5_0"):
    a = actors.get(label)
    if not a: lines.append("# missing " + label); continue
    tris = triangles(a)
    lines.append("# %s: %d tris" % (label, len(tris)))
    for tri in tris:
        zs = [p.z for p in tri]
        if max(zs) - min(zs) < 1.0 and 320 <= zs[0] <= 380:
            lines.append("  flat2F z=%.0f %s" % (zs[0], " ".join("(%.0f,%.0f)" % (p.x, p.y) for p in tri)))
open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
unreal.log("[StairSurvey] wrote %d lines" % len(lines))
