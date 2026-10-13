"""Open the west stair hall of the Main map: remove the corridor wall + door that
place_main_room_doors.py put across the WEST staircase bays, rebuild the navmesh, save Main.

The SketchUp model (actor 'Model') contains a third, switchback staircase at the west end
(lower run x=40..180 from y=-100 up to y=-380, landing, upper run x=200..340 arriving on 2F
at y=-240; 2F slab hole x=40..340, y=-240..-800). It is the mirror of the east stair
('계단_x1618', x=3960..4260). The room-door pass only knew the middle and east stair halls,
so bay 1F N (20..350) and bay 2F N (20..710) got a 20 cm wall with a sliding door, closing
the west stair off from the corridor (the 2F door even straddled the stairwell edge at x=340).

Removes: YUFS_Room_1F_N_00_* and YUFS_Room_2F_N_00_* (wall W/E, lintel, door) = 8 actors.
Leaves every other room wall/door, the stairs and the exits untouched.
Run:  UnrealEditor-Cmd.exe YUFS.uproject -ExecutePythonScript=<this file>   (editor ticks needed
for the navmesh rebuild; a pythonscript commandlet cannot rebuild navigation).
"""
import os
import unreal

MAP = "/Game/Maps/Main"
REMOVE_PREFIXES = ("YUFS_Room_1F_N_00_", "YUFS_Room_2F_N_00_")
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
state = {"ticks": 0, "quiet": 0, "handle": None}


def log(msg):
    unreal.log("[YUFSStairHalls] " + msg)


def tick(delta):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    state["ticks"] += 1
    building = unreal.NavigationSystemV1.is_navigation_being_built(world)
    state["quiet"] = 0 if building else state["quiet"] + 1
    if state["ticks"] < 30 or state["quiet"] < 60:
        if state["ticks"] % 300 == 0:
            log("navmesh building=%s ticks=%d" % (building, state["ticks"]))
        return
    unreal.unregister_slate_post_tick_callback(state["handle"])
    saved = les.save_current_level()
    log("navmesh rebuilt after %d ticks; saved=%s" % (state["ticks"], saved))
    if not saved:
        unreal.log_error("[YUFSStairHalls] Main was NOT saved (is another editor holding Main.umap?)")
    unreal.SystemLibrary.quit_editor()


def main():
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    if not les.load_level(MAP):
        log("could not load " + MAP)
        unreal.SystemLibrary.quit_editor()
        return
    removed = []
    for a in eas.get_all_level_actors():
        label = a.get_actor_label()
        if label.startswith(REMOVE_PREFIXES):
            removed.append(label)
            eas.destroy_actor(a)
    log("removed %d actors: %s" % (len(removed), ", ".join(sorted(removed))))
    if len(removed) != 8:
        unreal.log_warning("[YUFSStairHalls] expected 8 actors (two bays x wallW/wallE/lintel/door), found %d" % len(removed))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    state["handle"] = unreal.register_slate_post_tick_callback(tick)


main()
