"""PIE-only check of all three ground-floor exits (west, central, east); saves nothing.

For every YUFSExitPoint: physical capsule sweep through the portal, navmesh projection of the
exterior target, and a synchronous path query from inside the corridor to that target.
Then an ordinary Start with the test NPCs and a 90 s observation; the log lists which exit
each NPC physically cleared. Run with the editor's -ExecutePythonScript and
-YUFSSpawnTestNPCs=20, not a Python commandlet.
"""
import time
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level('/Game/Maps/Main')
start = time.monotonic()
checked = False
CORRIDOR_Z = 90.0
# Interior path starts: next to each end wall, and the lobby in the middle of the corridor.
PATH_STARTS = {'WestEnd': [(300, -920), (2150, -920)], 'EastEnd': [(4000, -920), (2150, -920)],
               'MainEntrance': [(2150, -920), (300, -920)]}


def log(message):
    unreal.log('[THREE_EXIT_PIE] ' + message)


def check_exits(world):
    exit_class = unreal.load_class(None, '/Script/YUFS.YUFSExitPoint')
    for exit_actor in unreal.GameplayStatics.get_all_actors_of_class(world, exit_class):
        exit_id = str(exit_actor.get_editor_property('ExitID'))
        enabled = exit_actor.get_editor_property('bPhysicalPassageEnabled')
        p = exit_actor.get_actor_location()
        direction = exit_actor.get_actor_forward_vector()
        a = p - direction * 100 + unreal.Vector(0, 0, CORRIDOR_Z)
        b = p + direction * 100 + unreal.Vector(0, 0, CORRIDOR_Z)
        clear = exit_actor.validate_physical_travel(a, b, 42.0, 88.0, exit_actor)
        crosses = exit_actor.crosses_outward(a, b, 42.0, 88.0) if hasattr(exit_actor, 'crosses_outward') else 'n/a'
        target = exit_actor.get_navigation_destination()
        projected = unreal.NavigationSystemV1.project_point_to_navigation(world, target, None, None, unreal.Vector(50, 50, 120))
        if isinstance(projected, tuple):
            projected = projected[0]
        on_nav = projected is not None and not (projected.x == 0 and projected.y == 0 and projected.z == 0)
        log('%s enabled=%s capsuleClear=%s portalCross=%s target=%s projectedOnNavmesh=%s (%s)' % (
            exit_id, enabled, clear, crosses, target, on_nav, projected))
        for sx, sy in PATH_STARTS.get(exit_id, [(2150, -920)]):
            path = unreal.NavigationSystemV1.find_path_to_location_synchronously(
                world, unreal.Vector(sx, sy, CORRIDOR_Z), target, None, None)
            ok = path is not None and path.is_valid() and not path.is_partial()
            points = path.path_points if path is not None else []
            log('%s path from (%d,%d): valid=%s partial=%s points=%d last=%s' % (
                exit_id, sx, sy, path.is_valid() if path else None, path.is_partial() if path else None,
                len(points), points[-1] if points else None))
            if not ok:
                unreal.log_warning('[THREE_EXIT_PIE] %s has NO complete path from (%d,%d)' % (exit_id, sx, sy))


def tick(dt):
    global checked
    elapsed = time.monotonic() - start
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if world and not checked and elapsed > 5:
            checked = True
            npcs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.load_class(None, '/Script/YUFS.YUFSEvacuationNPC'))
            log('npcCount=%d' % len(npcs))
            check_exits(world)
            controllers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.load_class(None, '/Script/YUFS.YUFSSimulationController'))
            if controllers:
                controllers[0].start_simulation()
                log('ordinary Start invoked; no broadcast/risk/personality overrides')
        if elapsed > 95:
            log('observation window complete')
            unreal.unregister_slate_post_tick_callback(handle)
            editor.editor_request_end_play()
            unreal.SystemLibrary.quit_editor()
    except Exception as error:
        unreal.log_error('[THREE_EXIT_PIE] ' + str(error))
        unreal.unregister_slate_post_tick_callback(handle)
        editor.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()


handle = unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
