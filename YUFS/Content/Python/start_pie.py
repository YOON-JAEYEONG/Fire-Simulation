import unreal


def start_pie_in_editor():
    """Start Play-In-Editor in the loaded building map (used by the Launch-*.ps1 helpers)."""
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    unreal.log("[YUFSLaunch] Requesting Play-In-Editor for the loaded map")
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    level_editor.editor_request_begin_play()


start_pie_in_editor()
