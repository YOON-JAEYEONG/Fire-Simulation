"""Check the Main map's room doors against the saved navmesh (run after place_main_room_doors.py).

For every YUFS_Room_*_Door: the doorway must be on the navmesh, the wall beside it must not be,
and the path from the room to the corridor must cross the wall line inside the doorway.
Output: Saved/main_room_door_check.txt.  Run: UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
"""
import os
import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "main_room_door_check.txt")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/Maps/Main")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
lines, ok_count, bad = [], 0, []


def on_nav(p, extent=15.0):
    q = unreal.NavigationSystemV1.project_point_to_navigation(
        world, p, None, None, unreal.Vector(extent, extent, 120.0))
    if isinstance(q, tuple):
        q = q[0]
    return q is not None and not (q.x == 0 and q.y == 0 and q.z == 0)


for door in sorted((a for a in eas.get_all_level_actors() if a.get_actor_label().startswith("YUFS_Room_")
                    and a.get_actor_label().endswith("_Door")), key=lambda a: a.get_actor_label()):
    label = door.get_actor_label()
    hinge = door.get_actor_location()
    leaf = 100.0 * door.get_actor_scale3d().y
    mid_x, wall_y, z0 = hinge.x + leaf / 2.0, hinge.y, hinge.z - 1.0
    into_room = 1.0 if wall_y > -900.0 else -1.0
    doorway = on_nav(unreal.Vector(mid_x, wall_y, z0 + 50.0))
    wall = on_nav(unreal.Vector(mid_x - leaf - 60.0, wall_y, z0 + 50.0), 5.0)
    start = unreal.Vector(mid_x + 30.0, wall_y + into_room * 160.0, z0 + 90.0)
    end = unreal.Vector(mid_x + 40.0, wall_y - into_room * 120.0, z0 + 90.0)
    path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, end)
    pts = list(path.path_points) if path else []
    cross = None
    for a, b in zip(pts, pts[1:]):
        if (a.y - wall_y) * (b.y - wall_y) <= 0 and abs(b.y - a.y) > 1e-3:
            t = (wall_y - a.y) / (b.y - a.y)
            cross = a.x + t * (b.x - a.x)
            break
    through = cross is not None and hinge.x - 2.0 <= cross <= hinge.x + leaf + 2.0
    good = doorway and not wall and through and not path.is_partial()
    ok_count += good
    if not good:
        bad.append(label)
    lines.append("%s %s doorwayOnNav=%s wallOnNav=%s pathPts=%d partial=%s cross=%s" % (
        "OK " if good else "BAD", label, doorway, wall, len(pts), path.is_partial() if path else None,
        "%.0f" % cross if cross is not None else "none"))
    if not good:
        lines.append("#   path " + " ".join("(%.0f,%.0f,%.0f)" % (p.x, p.y, p.z) for p in pts))
        for sx in range(40, 720, 40):
            lines.append("#   wall line x=%d onNav=%s" % (sx, on_nav(unreal.Vector(sx, wall_y, z0 + 50.0), 5.0)))

lines.insert(0, "# %d/%d doors OK%s" % (ok_count, len(lines), (" bad: " + ", ".join(bad)) if bad else ""))
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
unreal.log("[YUFSDoorCheck] %s" % lines[0])
