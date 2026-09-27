"""Place left-behind bags (AYUFSBelongingsBag) in the Main (IT building) map and save it.

Positions are walkable spots on 1F/2F inside the it_test fire data domain (taken from
NPC spawn points of a headless test run). Each bag is dropped onto the floor with a
downward trace. Re-running replaces the bags this script placed (label prefix YUFS_Bag_).

Run:  UnrealEditor.exe YUFS.uproject /Game/Maps/Main -ExecutePythonScript=<this file>
Set YUFS_BAGS_QUIT=1 to close the editor after saving.
"""
import os
import unreal

MAP = "/Game/Maps/Main"
LABEL_PREFIX = "YUFS_Bag_"
# (x, y, approximate floor z, yaw) -- 1F floor z=0, 2F floor z=350.
SPOTS = [
    (3569.0, -709.0, 0.0, 30.0),
    (1458.0, -953.0, 0.0, 120.0),
    (2322.0, -1520.0, 0.0, 200.0),
    (2860.0, -950.0, 0.0, 75.0),
    (826.0, -1158.0, 350.0, 300.0),
    (3893.0, -1276.0, 350.0, 15.0),
    (2676.0, -880.0, 350.0, 250.0),
    (1637.0, -1450.0, 350.0, 160.0),
]
BAG_HALF_HEIGHT = 20.0


def floor_z(world, x, y, approx_z):
    start = unreal.Vector(x, y, approx_z + 150.0)
    end = unreal.Vector(x, y, approx_z - 120.0)
    hit = unreal.SystemLibrary.line_trace_single(
        world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
        unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    t = hit.to_tuple()
    blocking = t[0]
    impact = t[5]  # ImpactPoint
    return impact.z if blocking else None


def main():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not level_editor.load_level(MAP):
        unreal.log_error("[YUFSBags] Could not load " + MAP)
        return
    world = world_sub.get_editor_world()

    removed = 0
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith(LABEL_PREFIX):
            actors.destroy_actor(actor)
            removed += 1

    bag_class = unreal.load_class(None, "/Script/YUFS.YUFSBelongingsBag")
    placed = 0
    for index, (x, y, z, yaw) in enumerate(SPOTS, start=1):
        ground = floor_z(world, x, y, z)
        if ground is None:
            unreal.log_warning("[YUFSBags] No floor under (%.0f, %.0f, %.0f); using the listed height" % (x, y, z))
            ground = z
        location = unreal.Vector(x, y, ground + BAG_HALF_HEIGHT)
        bag = actors.spawn_actor_from_class(bag_class, location, unreal.Rotator(0.0, 0.0, yaw))
        if not bag:
            unreal.log_error("[YUFSBags] Failed to spawn bag %d" % index)
            continue
        floor_name = "1F" if z < 175.0 else "2F"
        bag.set_actor_label("%s%s_%02d" % (LABEL_PREFIX, floor_name, index))
        bag.set_folder_path("YUFS/Belongings")
        placed += 1
        unreal.log("[YUFSBags] %s at %s" % (bag.get_actor_label(), location))

    saved = level_editor.save_current_level()
    unreal.log("[YUFSBags] removed=%d placed=%d saved=%s" % (removed, placed, saved))
    if os.environ.get("YUFS_BAGS_QUIT") == "1":
        unreal.SystemLibrary.quit_editor()


main()
