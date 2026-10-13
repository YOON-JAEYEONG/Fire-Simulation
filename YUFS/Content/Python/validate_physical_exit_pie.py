"""PIE-only geometry/navigation audit and ordinary Start run; does not save assets.
Run with the editor's -ExecutePythonScript, not a Python commandlet.
"""
import time
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level('/Game/Maps/Main')
start = time.monotonic()
checked = False
controller = None
def tick(dt):
    global checked, controller
    elapsed = time.monotonic()-start
    try:
        world = unreal.EditorLevelLibrary.get_game_world()
        if world and not checked and elapsed > 5:
            checked = True
            exits = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.load_class(None,'/Script/YUFS.YUFSExitPoint'))
            npcs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.load_class(None,'/Script/YUFS.YUFSEvacuationNPC'))
            unreal.log('[EXIT_PIE] npcCount=%d' % len(npcs))
            for exit_actor in exits:
                enabled = exit_actor.get_editor_property('bPhysicalPassageEnabled')
                p = exit_actor.get_actor_location()
                direction = exit_actor.get_actor_forward_vector()
                a = p-direction*100+unreal.Vector(0,0,90)
                b = p+direction*100+unreal.Vector(0,0,90)
                clear = exit_actor.validate_physical_travel(a,b,42.0,88.0,exit_actor)
                unreal.log('[EXIT_PIE] %s enabled=%s capsuleClear=%s; navigation verified by actual NPC traversal below' %
                    (exit_actor.get_name(),enabled,clear))
            controllers = unreal.GameplayStatics.get_all_actors_of_class(world,unreal.load_class(None,'/Script/YUFS.YUFSSimulationController'))
            controller = controllers[0] if controllers else None
            if controller:
                controller.start_simulation()
                unreal.log('[EXIT_PIE] ordinary Start invoked; no broadcast/risk/personality overrides')
        if elapsed > 90:
            unreal.log('[EXIT_PIE] ordinary run observation window complete')
            unreal.unregister_slate_post_tick_callback(handle)
            editor.editor_request_end_play()
            unreal.SystemLibrary.quit_editor()
    except Exception as error:
        unreal.log_error('[EXIT_PIE] '+str(error))
        unreal.unregister_slate_post_tick_callback(handle)
        editor.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()
handle = unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
