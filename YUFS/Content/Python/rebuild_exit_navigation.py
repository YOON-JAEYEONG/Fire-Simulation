"""Editor-only navigation bake for the modified Main exit apron; saves Main only."""
import time
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level('/Game/Maps/Main')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
started = time.monotonic()
def tick(dt):
    if time.monotonic()-started < 2 or unreal.NavigationSystemV1.is_navigation_being_built(world):
        return
    unreal.unregister_slate_post_tick_callback(handle)
    assert editor.save_current_level()
    unreal.log('[EXIT_NAV_BAKE] build completed; Main saved')
    unreal.SystemLibrary.quit_editor()
handle = unreal.register_slate_post_tick_callback(tick)
