"""Bake navigation for Main_level and save its editor overview."""
import unreal,time
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/Maps/it관_찐최종/Main_level')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
location=unreal.Vector(6000,-13200,5500)
unreal.EditorLevelLibrary.set_level_viewport_camera_info(location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(6000,-8650,100)))
unreal.SystemLibrary.execute_console_command(world,'RebuildNavigation')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started=time.monotonic()
def tick(dt):
    if time.monotonic()-started<3 or unreal.NavigationSystemV1.is_navigation_being_built(world):return
    unreal.unregister_slate_post_tick_callback(handle)
    assert level.save_current_level()
    unreal.log('[MERGE_FINAL_NAV] bake completed; map saved')
    unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
