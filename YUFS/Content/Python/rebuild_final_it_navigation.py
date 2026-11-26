"""Rebuild Main_level's static NavMesh after changing its doors or walls.

Run in Unreal Editor. The imported rooms have narrow door-threshold seams:
the previous 8x5 cm voxel grid also left several open upstairs thresholds
disconnected. A 4x3 cm bake joins the physically passable openings without
changing the authored agent radius, step limit, collisions, or other levels.
"""

import time
import os

import unreal


MAP = '/Game/Maps/it관_찐최종/Main_level'


def rebuild():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert not level.is_in_play_in_editor(), 'Stop PIE before rebuilding Main_level'
    assert level.load_level(MAP), MAP
    world = editor.get_editor_world()
    nav_meshes = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    assert nav_meshes, 'Main_level has no authored RecastNavMesh'
    for nav_mesh in nav_meshes:
        params = nav_mesh.get_editor_property('nav_mesh_resolution_params')
        for i, resolution in enumerate(params):
            resolution.set_editor_property('cell_size', 4.0)
            resolution.set_editor_property('cell_height', 3.0)
            params[i] = resolution
        nav_mesh.set_editor_property('nav_mesh_resolution_params', params)

    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
    started = time.monotonic()

    def save_when_built(_):
        elapsed = time.monotonic() - started
        if elapsed < 1.0:
            return
        if unreal.NavigationSystemV1.is_navigation_being_built(world):
            if elapsed < 60.0:
                return
            unreal.unregister_slate_post_tick_callback(handle)
            raise RuntimeError('Main_level navigation did not finish; level not saved')
        unreal.unregister_slate_post_tick_callback(handle)
        assert editor.get_editor_world() == world, 'Level changed; not saving'
        assert level.save_current_level(), 'Could not save Main_level'
        unreal.log('[YUFSMainLevelNavigation] Rebuilt and saved 4x3 cm static NavMesh')
        if os.environ.get('YUFS_QUIT_AFTER_NAV_BAKE') == '1':
            unreal.SystemLibrary.quit_editor()

    handle = unreal.register_slate_post_tick_callback(save_when_built)


if __name__ == '__main__':
    rebuild()
