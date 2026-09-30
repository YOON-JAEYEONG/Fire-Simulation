"""Give every room of the Main (IT building) map a corridor-side wall with a door, and save.

The modelled shell has no corridor walls: the partition walls stop at the corridor edges
(y=-800 on the north side, y=-1040 on the south side; survey_main_room_doors.py), so each room is
open to the corridor across its whole width. This closes each room's corridor side with a 20 cm
wall (engine cube, full storey) that leaves one doorway, and puts an AYUFSInteractionDoor in it,
so people in a room have to open the door to get out.

- Rooms are the bays between partition walls. The entrance hall (1F north, x=1810..2510) and the
  two stair halls stay open. A bay without navmesh inside (no floor) is skipped.
- The hinge sits HINGE_INSET cm in from the doorway's west jamb, so at 90 degrees the leaf's swept
  box (AYUFSInteractionDoor::CanSweepLeaf) clears the jamb whichever way the door swings.
- The doorway is wide enough for the navmesh agent (2 * AgentRadius + NAV_CLEARANCE).
- The navmesh is rebuilt after the walls are added (full editor only, not a commandlet) and the
  level is saved once the build has finished. Re-running replaces what this script placed
  (actor label prefix YUFS_Room_).
Run:  UnrealEditor.exe YUFS.uproject /Game/Maps/Main -ExecutePythonScript=<this file>
      Set YUFS_QUIT_AFTER=1 to close the editor after saving.
"""
import os
import unreal

MAP = "/Game/Maps/Main"
PREFIX = "YUFS_Room_"
STOREY = 350.0
WALL_T = 20.0
LEAF_H = 220.0
LINTEL_GAP = 6.0          # leaf top to lintel
HINGE_INSET = 8.0         # hinge distance from the west jamb (and latch-side gap)
MIN_LEAF = 100.0
NAV_CLEARANCE = 40.0
DOOR_OPEN_SECONDS = 1.2   # AYUFSInteractionDoor default; evacuees push the door open quickly
# Corridor-side wall line of the rooms: north rooms end at y=-800, south rooms at y=-1040.
SIDES = {"N": (-800.0, +1.0, -400.0), "S": (-1040.0, -1.0, -1440.0)}  # corridor face y, into-room sign, room probe y
# Bays between partition walls (inner faces), from survey_main_room_doors.py.
BAYS_1F = [(20, 350), (370, 710), (730, 1070), (1090, 1430), (1450, 1790), (1810, 2510),
           (2530, 2870), (2890, 3230), (3250, 3590), (3610, 3950), (3970, 4280)]
BAYS = {
    ("1F", "N"): BAYS_1F,
    ("1F", "S"): BAYS_1F,
    ("2F", "N"): [(20, 710), (730, 1070), (1090, 1430), (1450, 1790), (1810, 2510),
                  (2530, 2870), (2890, 3230), (3250, 3590), (3610, 4280)],
    ("2F", "S"): BAYS_1F,
}
FLOOR_Z = {"1F": 0.0, "2F": 350.0}
ENTRANCE_HALL = ("1F", "N", 1810)
# Stair halls (x range, side): NE stair 3960..4260 north, middle stair 2000..2300 south.
STAIRS = [((3960.0, 4260.0), "N"), ((2000.0, 2300.0), "S")]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
state = {"phase": "wait_build", "ticks": 0, "quiet": 0, "handle": None}


def log(msg):
    unreal.log("[YUFSRoomDoors] " + msg)


def on_nav(world, x, y, z):
    p = unreal.NavigationSystemV1.project_point_to_navigation(
        world, unreal.Vector(x, y, z + 50.0), None, None, unreal.Vector(60.0, 60.0, 120.0))
    if isinstance(p, tuple):
        p = p[0]
    return p is not None and not (p.x == 0 and p.y == 0 and p.z == 0)


def spawn_box(label, cx, cy, cz, sx, sy, sz, cube, material):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(cx, cy, cz), unreal.Rotator(0.0, 0.0, 0.0))
    smc = a.static_mesh_component
    smc.set_static_mesh(cube)
    if material:
        smc.set_material(0, material)
    a.set_actor_scale3d(unreal.Vector(sx / 100.0, sy / 100.0, sz / 100.0))
    a.set_actor_label(label)
    a.set_folder_path("YUFS/RoomWalls")
    return a


def place(world):
    removed = 0
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith(PREFIX):
            eas.destroy_actor(a)
            removed += 1
    navmesh = next((a for a in eas.get_all_level_actors() if a.get_class().get_name() == "RecastNavMesh"), None)
    radius = float(navmesh.get_editor_property("agent_radius")) if navmesh else 34.0
    leaf = max(MIN_LEAF, 2.0 * radius + NAV_CLEARANCE)
    opening = leaf + 2.0 * HINGE_INSET
    log("removed=%d agentRadius=%.0f leaf=%.0f opening=%.0f" % (removed, radius, leaf, opening))

    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    material = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")
    door_class = unreal.load_class(None, "/Script/YUFS.YUFSInteractionDoor")
    doors = 0
    for (floor, side), bays in BAYS.items():
        z0 = FLOOR_Z[floor]
        face_y, into_room, probe_y = SIDES[side]
        wall_y = face_y + into_room * WALL_T / 2.0
        for index, (xa, xb) in enumerate(bays):
            tag = "%s%s_%s_%02d" % (PREFIX, floor, side, index)
            if (floor, side, xa) == ENTRANCE_HALL:
                log("%s skipped: entrance hall" % tag)
                continue
            if any(side == s and xa < hi and xb > lo for (lo, hi), s in STAIRS):
                log("%s skipped: stair hall" % tag)
                continue
            if not on_nav(world, (xa + xb) / 2.0, probe_y, z0):
                log("%s skipped: no walkable floor in the bay" % tag)
                continue
            mid = (xa + xb) / 2.0
            left, right = mid - opening / 2.0, mid + opening / 2.0
            if left - xa < 20.0 or xb - right < 20.0:
                log("%s skipped: bay too narrow for a %.0f cm doorway" % (tag, opening))
                continue
            head = LEAF_H + LINTEL_GAP
            zc = z0 + STOREY / 2.0
            spawn_box(tag + "_WallW", (xa + left) / 2.0, wall_y, zc, left - xa, WALL_T, STOREY, cube, material)
            spawn_box(tag + "_WallE", (right + xb) / 2.0, wall_y, zc, xb - right, WALL_T, STOREY, cube, material)
            spawn_box(tag + "_Lintel", mid, wall_y, z0 + (head + STOREY) / 2.0, opening, WALL_T, STOREY - head,
                      cube, material)
            # Hinge on the floor at the west jamb (+inset); leaf closed along world +X (yaw -90).
            door = eas.spawn_actor_from_class(door_class, unreal.Vector(left + HINGE_INSET, wall_y, z0 + 1.0),
                                              unreal.Rotator(0.0, 0.0, -90.0))
            door.set_actor_scale3d(unreal.Vector(1.0, leaf / 100.0, 1.0))
            # Swing time (class default 1.2 s). The NPC walks up to the leaf and reaches for the
            # handle first, so the opening reads as an action without slowing the evacuation.
            door.set_editor_property("OpenSeconds", DOOR_OPEN_SECONDS)
            door.set_actor_label(tag + "_Door")
            door.set_folder_path("YUFS/RoomDoors")
            doors += 1
            log("%s door x=%.0f..%.0f y=%.0f z=%.0f" % (tag, left, right, wall_y, z0))
    log("placed %d doors" % doors)
    return doors


def tick(delta):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    state["ticks"] += 1
    building = unreal.NavigationSystemV1.is_navigation_being_built(world)
    state["quiet"] = 0 if building else state["quiet"] + 1
    # Wait until the rebuild has started and then stayed finished for a while (async tiles).
    if state["ticks"] < 30 or state["quiet"] < 60:
        if state["ticks"] % 300 == 0:
            log("navmesh building=%s ticks=%d" % (building, state["ticks"]))
        return
    unreal.unregister_slate_post_tick_callback(state["handle"])
    saved = les.save_current_level()
    log("navmesh rebuilt after %d ticks; saved=%s" % (state["ticks"], saved))
    if os.environ.get("YUFS_QUIT_AFTER") == "1":
        unreal.SystemLibrary.quit_editor()


def main():
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    if not les.load_level(MAP):
        log("could not load " + MAP)
        return
    if os.environ.get("YUFS_DOORS_TIMING_ONLY") == "1":
        # Door timing only: no walls or navmesh change, so this also works as a commandlet.
        doors = [a for a in eas.get_all_level_actors()
                 if a.get_actor_label().startswith(PREFIX) and a.get_actor_label().endswith("_Door")]
        for door in doors:
            door.set_editor_property("OpenSeconds", DOOR_OPEN_SECONDS)
        log("OpenSeconds=%.1f on %d doors; saved=%s" % (DOOR_OPEN_SECONDS, len(doors), les.save_current_level()))
        return
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if place(world) == 0:
        return
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    state["handle"] = unreal.register_slate_post_tick_callback(tick)


main()
