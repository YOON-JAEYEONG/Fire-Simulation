import os
import unreal

# Optional: YUFS_START_MAP=/Game/Maps/Main loads that level before Play-In-Editor.
START_MAP = os.environ.get("YUFS_START_MAP", "")


def start_pie_in_editor():
    """Start Play-In-Editor in the loaded building map (used by the Launch-*.ps1 helpers)."""
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if START_MAP:
        unreal.log("[YUFSLaunch] Loading " + START_MAP)
        level_editor.load_level(START_MAP)
    unreal.log("[YUFSLaunch] Requesting Play-In-Editor for the loaded map")
    level_editor.editor_request_begin_play()


start_pie_in_editor()
