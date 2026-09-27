"""Place the real exit of the Main (IT building) map, plus the level data manager, and save.

Where the exit comes from (survey_main_exits.py): slicing the outer wall meshes at
z=30/100/180 cm shows exactly ONE opening in the building shell on floor 1 - the north
wall at x=2070..2240 cm (1.7 m wide) in the lobby between the two stair wells. The same
slice on floor 2 has no opening there, so it is a door, not a window. Every other outer
wall segment is closed. The exit point sits on the navmesh just inside that doorway.

Re-running replaces what this script placed (label prefix YUFS_Exit_ / YUFS_LevelData).
Run headless:  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
"""
import unreal

MAP = "/Game/Maps/Main"
EXITS = [
    # (label, ExitID, doorway centre x, y, width cm, familiar entry)
    ("YUFS_Exit_MainEntrance", "MainEntrance", 2155.0, 0.0, 170.0, True),
]
EXIT_HEIGHT = 90.0


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
    for label, exit_id, x, y, width, familiar in EXITS:
        # Walk inward from the doorway until the navmesh is hit, so NPCs can reach the point.
        ground = None
        for inset in range(40, 241, 20):
            p = unreal.NavigationSystemV1.project_point_to_navigation(
                world, unreal.Vector(x, y - inset, 50.0), None, None, unreal.Vector(20.0, 20.0, 80.0))
            if isinstance(p, tuple):
                p = p[0]
            if p is not None and not (p.x == 0 and p.y == 0 and p.z == 0):
                ground = p
                break
        if ground is None:
            unreal.log_error("[YUFSExits] no navmesh inside doorway %s" % label)
            continue
        exit_actor = actors.spawn_actor_from_class(exit_class, unreal.Vector(ground.x, ground.y, ground.z + EXIT_HEIGHT),
                                                   unreal.Rotator(0.0, 0.0, 90.0))
        exit_actor.set_actor_label(label)
        exit_actor.set_folder_path("YUFS/Exits")
        exit_actor.set_editor_property("ExitID", exit_id)
        exit_actor.set_editor_property("ExitWidth", width)
        exit_actor.set_editor_property("bIsFamiliarEntry", familiar)
        unreal.log("[YUFSExits] %s (%s) at %s" % (label, exit_id, exit_actor.get_actor_location()))

    if not any(a.get_class().get_name() == "YUFSLevelDataManager" for a in actors.get_all_level_actors()):
        manager = actors.spawn_actor_from_class(unreal.load_class(None, "/Script/YUFS.YUFSLevelDataManager"),
                                                unreal.Vector(2150.0, -920.0, 0.0), unreal.Rotator())
        manager.set_actor_label("YUFS_LevelData")
        manager.set_folder_path("YUFS")
        unreal.log("[YUFSExits] placed YUFSLevelDataManager")
    saved = level_editor.save_current_level()
    unreal.log("[YUFSExits] removed=%d saved=%s" % (removed, saved))


main()
