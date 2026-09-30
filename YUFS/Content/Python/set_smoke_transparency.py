"""Make the it_test smoke more see-through by lowering Density Scale on the fire material instances.

Only the rendered smoke changes. NPC smoke/heat perception reads the BIN data and is not
affected. The factor is applied to each instance's PARENT default, so re-running does not
compound; change DENSITY_FACTOR (or YUFS_SMOKE_DENSITY) to tune it. 1.0 = original thickness.

Main uses the team's MI_Fire1..3 (Fires/Audit0928) since the merge of merge_JJW_Kim_Eung_Gang;
the earlier imported fire_it_test_*_MIC are kept in step for maps that still use them.
Run:  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
      (set YUFS_START_PIE=1 when running inside the editor to start Play afterwards)
"""
import os
import unreal

TARGETS = [
    "/Game/Fires/Audit0928/MI_Fire1",
    "/Game/Fires/Audit0928/MI_Fire2",
    "/Game/Fires/Audit0928/MI_Fire3",
    "/Game/Fires/FirePrototype/VDB/fire_it_test_1_MIC",
    "/Game/Fires/FirePrototype/VDB/fire_it_test_2_MIC",
    "/Game/Fires/FirePrototype/VDB/fire_it_test_3_MIC",
]
DENSITY_PARAM = "Density Scale"
DENSITY_FACTOR = float(os.environ.get("YUFS_SMOKE_DENSITY", "0.35"))

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[YUFSSmokeAlpha] " + msg)


def main():
    changed = 0
    for path in TARGETS:
        if not eal.does_asset_exist(path):
            log("skip (not in this project) " + path)
            continue
        mic = eal.load_asset(path)
        parent = mic.get_editor_property("parent")
        if not parent or DENSITY_PARAM not in [str(n) for n in mel.get_scalar_parameter_names(parent)]:
            unreal.log_warning("[YUFSSmokeAlpha] %s: parent has no '%s'" % (path, DENSITY_PARAM))
            continue
        default = mel.get_material_default_scalar_parameter_value(parent, DENSITY_PARAM)
        before = mel.get_material_instance_scalar_parameter_value(mic, DENSITY_PARAM)
        target = default * DENSITY_FACTOR
        mel.set_material_instance_scalar_parameter_value(mic, DENSITY_PARAM, target)
        mel.update_material_instance(mic)
        eal.save_asset(path, only_if_is_dirty=False)
        changed += 1
        log("%s %s: %.4f -> %.4f (parent %s default %.4f x %.2f)" % (
            path, DENSITY_PARAM, before, target, parent.get_name(), default, DENSITY_FACTOR))
    log("done: %d material instances" % changed)
    if os.environ.get("YUFS_START_PIE") == "1":
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        level_editor.load_level("/Game/Maps/Main")
        level_editor.editor_request_begin_play()
        log("Play-In-Editor requested")


main()
