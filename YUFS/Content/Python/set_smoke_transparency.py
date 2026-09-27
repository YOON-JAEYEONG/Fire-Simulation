"""Make the it_test smoke more see-through by lowering Density Scale on the fire MICs.

Only the rendered smoke changes. NPC smoke/heat perception reads the BIN data and
is not affected. Re-run with a different DENSITY_FACTOR to tune it; the factor is
applied to the PARENT material's default, so running it twice does not compound.
"""
import unreal

DEST = "/Game/Fires/FirePrototype/VDB"
PARENT = "/Game/Fires/Material/SparseVolumeMaterial"
DENSITY_PARAM = "Density Scale"
DENSITY_FACTOR = 0.35  # 1.0 = original thickness, lower = more transparent
START_PIE = True

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[YUFSSmokeAlpha] " + msg)


def main():
    parent = eal.load_asset(PARENT)
    names = [str(n) for n in mel.get_scalar_parameter_names(parent)]
    log("parent scalar params: %s" % names)
    if DENSITY_PARAM not in names:
        unreal.log_error("[YUFSSmokeAlpha] %s not found on parent" % DENSITY_PARAM)
        return
    default = mel.get_material_default_scalar_parameter_value(parent, DENSITY_PARAM)
    target = default * DENSITY_FACTOR
    for index in (1, 2, 3):
        path = "%s/fire_it_test_%d_MIC" % (DEST, index)
        if not eal.does_asset_exist(path):
            unreal.log_warning("[YUFSSmokeAlpha] missing " + path)
            continue
        mic = eal.load_asset(path)
        before = mel.get_material_instance_scalar_parameter_value(mic, DENSITY_PARAM)
        mel.set_material_instance_scalar_parameter_value(mic, DENSITY_PARAM, target)
        mel.update_material_instance(mic)
        eal.save_asset(path, only_if_is_dirty=False)
        log("%s %s: %.4f -> %.4f (parent default %.4f x %.2f)" % (path, DENSITY_PARAM, before, target, default, DENSITY_FACTOR))
    log("done")
    if START_PIE:
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        level_editor.load_level("/Game/Maps/Main")
        level_editor.editor_request_begin_play()
        log("Play-In-Editor requested")


main()
