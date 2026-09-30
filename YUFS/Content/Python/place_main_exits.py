"""Place the exits of the Main (IT building) map, plus the level data manager, and save.

The real IT building has three ground-floor exits: one at each end of the building and one
in the middle (confirmed by the team, 2026-09-29).

- MainEntrance: survey_main_exits.py found exactly one opening in the modelled shell, the
  north wall at x=2070..2240 cm (1.7 m) in the lobby between the two stair wells. Familiar
  entry (the door people use every day).
- WestEnd / EastEnd: the modelled end walls are closed, so these sit where the ground-floor
  corridor (navmesh band y=-860..-980, centre y=-920, see Saved/main_nav_f1.txt) meets the
  west (x=0) and east (x=4300) walls. The model has no hole there, so each exit draws a door
  leaf + frame + EXIT sign on the wall; NPCs are counted as evacuated on reaching the point.
  Emergency exits, not familiar entries.

Each exit actor's forward (+X) points out of the building; its marker frame is drawn on the
inner wall face. Re-running replaces what this script placed (labels YUFS_Exit_* /
YUFS_LevelData).
Run headless:  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
"""
import unreal

MAP = "/Game/Maps/Main"
CORRIDOR_Y = -920.0
EXITS = [
    # label, ExitID, doorway centre on the outer wall (x, y), inward unit (dx, dy),
    # inner wall-face coordinate along the inward axis, yaw facing out, width cm, familiar, draw door leaf
    ("YUFS_Exit_MainEntrance", "MainEntrance", (2155.0, 0.0), (0.0, -1.0), -20.0, 90.0, 170.0, True, False),
    ("YUFS_Exit_WestEnd", "WestEnd", (0.0, CORRIDOR_Y), (1.0, 0.0), 20.0, 180.0, 180.0, False, True),
    ("YUFS_Exit_EastEnd", "EastEnd", (4300.0, CORRIDOR_Y), (-1.0, 0.0), 4280.0, 0.0, 180.0, False, True),
]
EXIT_HEIGHT = 90.0


def project(world, x, y):
    p = unreal.NavigationSystemV1.project_point_to_navigation(
        world, unreal.Vector(x, y, 50.0), None, None, unreal.Vector(20.0, 20.0, 80.0))
    if isinstance(p, tuple):
        p = p[0]
    if p is None or (p.x == 0 and p.y == 0 and p.z == 0):
        return None
    return p


def main():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not level_editor.load_level(MAP):
        unreal.log_error("[YUFSExits] Could not load " + MAP)
        return
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    removed = 0
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith("YUFS_Exit_") or label == "YUFS_LevelData":
            actors.destroy_actor(actor)
            removed += 1

    exit_class = unreal.load_class(None, "/Script/YUFS.YUFSExitPoint")
    others = [a for a in actors.get_all_level_actors()
              if a.get_class().get_name() == "YUFSExitPoint" and not a.get_actor_label().startswith("YUFS_Exit_")]
    placed = 0
    for label, exit_id, (x, y), (dx, dy), wall_face, yaw, width, familiar, leaf in EXITS:
        # An exit the team already placed at this doorway stays; do not stack a second one on it.
        twin = next((o for o in others if abs(o.get_actor_location().x - x) < 300.0
                     and abs(o.get_actor_location().y - y) < 300.0), None)
        if twin:
            unreal.log("[YUFSExits] %s kept the existing exit %s" % (label, twin.get_actor_label()))
            continue
        # Walk inward from the doorway until the navmesh is hit, so NPCs can reach the point.
        ground = None
        for inset in range(40, 241, 20):
            ground = project(world, x + dx * inset, y + dy * inset)
            if ground is not None:
                break
        if ground is None:
            unreal.log_error("[YUFSExits] no navmesh inside doorway %s" % label)
            continue
        exit_actor = actors.spawn_actor_from_class(exit_class, unreal.Vector(ground.x, ground.y, ground.z + EXIT_HEIGHT),
                                                   unreal.Rotator(0.0, 0.0, yaw))
        exit_actor.set_actor_label(label)
        exit_actor.set_folder_path("YUFS/Exits")
        exit_actor.set_editor_property("ExitID", exit_id)
        exit_actor.set_editor_property("ExitWidth", width)
        exit_actor.set_editor_property("bIsFamiliarEntry", familiar)
        exit_actor.set_editor_property("bShowDoorLeaf", leaf)
        along = ground.x if dx else ground.y
        exit_actor.set_editor_property("MarkerWallDistance", max(0.0, abs(along - wall_face) - 1.0))
        exit_actor.set_editor_property("MarkerFloorOffset", EXIT_HEIGHT)
        placed += 1
        unreal.log("[YUFSExits] %s (%s) at %s familiar=%s wallDist=%.0f" % (
            label, exit_id, exit_actor.get_actor_location(), familiar,
            exit_actor.get_editor_property("MarkerWallDistance")))

    if not any(a.get_class().get_name() == "YUFSLevelDataManager" for a in actors.get_all_level_actors()):
        manager = actors.spawn_actor_from_class(unreal.load_class(None, "/Script/YUFS.YUFSLevelDataManager"),
                                                unreal.Vector(2150.0, -920.0, 0.0), unreal.Rotator())
        manager.set_actor_label("YUFS_LevelData")
        manager.set_folder_path("YUFS")
        unreal.log("[YUFSExits] placed YUFSLevelDataManager")
    saved = level_editor.save_current_level()
    unreal.log("[YUFSExits] removed=%d placed=%d saved=%s" % (removed, placed, saved))


main()
