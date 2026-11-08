"""Opt-in presentation: existing NPC and prop, no FDS or evacuation-result mutation."""
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor.load_level('/Game/Maps/Prototype')
started = time.monotonic()
last_attempt = 0.0
callback = None
audit = '-YUFSPresentationAudit' in unreal.SystemLibrary.get_command_line()
selected = None
selected_world = None
selected_at = 0.0
captured = False
ending_at = 0.0


def prepare(delta):
    global last_attempt, selected, selected_world, selected_at, captured, ending_at
    if time.monotonic() - last_attempt < 1:
        return
    last_attempt = time.monotonic()
    try:
        if ending_at:
            if time.monotonic() - ending_at > 3:
                unreal.unregister_slate_post_tick_callback(callback)
                unreal.SystemLibrary.quit_editor()
            return
        if selected:
            if audit and not captured and selected.get_extinguisher_state() == unreal.YUFSFireExtinguisherState.SPRAYING:
                player = unreal.GameplayStatics.get_player_controller(selected_world, 0)
                unreal.log('[SuppressionPresentation] screenshot view=%s' % player.get_view_target().get_name())
                unreal.SystemLibrary.execute_console_command(selected_world, 'HighResShot 1280x720 filename=/private/tmp/yufs-suppression-spray.png')
                captured = True
            if audit and time.monotonic() - selected_at > 25:
                unreal.log('[SuppressionPresentation] AUDIT screenshot=%s finalState=%s' % (captured, selected.get_extinguisher_state()))
                editor.editor_request_end_play()
                ending_at = time.monotonic()
            return
        world = unreal.EditorLevelLibrary.get_game_world()
        if not world:
            return
        tools = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.YUFSFireExtinguisher)
        residents = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.YUFSEvacuationNPC)
        controllers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.YUFSSimulationController)
        if tools and residents and controllers:
            pairs = sorted(((npc.get_actor_location().distance(tool.get_actor_location()), npc, tool)
                            for npc in residents for tool in tools), key=lambda item: item[0])
            for distance, npc, tool in pairs:
                origin = npc.get_actor_location()
                prop = tool.get_actor_location()
                if abs(origin.z - prop.z) > 160 or distance > 600:
                    continue
                direction = unreal.Vector(prop.x - origin.x, prop.y - origin.y, 0).normal()
                target = prop + direction * 250 + unreal.Vector(0, 0, 65)
                suppression = npc.get_component_by_class(unreal.YUFSNpcSuppressionComponent)
                if not suppression.start_visual_presentation(tool, target, 20.0):
                    continue
                # Game-world spawning belongs to the native adapter, not editor asset APIs.
                focused = suppression.focus_visual_presentation()
                controllers[0].set_editor_property('enable_timeline_recording', False)
                unreal.log('[SuppressionPresentation] camera=%s npc=%s tool=%s. Authored gesture target only; no FDS alignment claimed.' % (focused, npc.get_name(), tool.get_name()))
                selected = tool
                selected_world = world
                selected_at = time.monotonic()
                if not audit:
                    unreal.unregister_slate_post_tick_callback(callback)
                return
        if time.monotonic() - started > 90:
            unreal.log_error('[SuppressionPresentation] Setup timed out. Check NPCs, navigation, compatible animation bindings and -YUFSBuildingInteractions.')
            if audit:
                editor.editor_request_end_play()
                ending_at = time.monotonic()
            else:
                unreal.unregister_slate_post_tick_callback(callback)
    except Exception:
        unreal.log_error('[SuppressionPresentation] ' + traceback.format_exc())
        if audit:
            editor.editor_request_end_play()
            ending_at = time.monotonic()
        else:
            unreal.unregister_slate_post_tick_callback(callback)


callback = unreal.register_slate_post_tick_callback(prepare)
editor.editor_request_begin_play()
