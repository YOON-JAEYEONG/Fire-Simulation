import unreal


def start_demo_in_editor():
    """Start PIE so the opt-in extinguisher demo runs inside the loaded building map."""
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    unreal.log("[YUFSExtinguisherDemo] Requesting Play-In-Editor for the building demo")
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    level_editor.editor_request_begin_play()


start_demo_in_editor()
