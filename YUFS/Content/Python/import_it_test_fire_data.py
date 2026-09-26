"""Import the shared it_test VDB fire data and wire the materials the Main map expects.

Follows VDB_임포트_및_머티리얼_연결_가이드.md:
  1. import one .vdb per fire (the numbered sequence is imported as an animated SVT)
  2. create a material instance per SVT with Density Mask enabled
  3. Main's YUFSSimulationController FireOptions already reference
     /Game/Fires/FirePrototype/VDB/fire_it_test_{1,2,3}_MIC and
     Fires/FirePrototype/BinaryData/smoke_data_it_test_{1,2,3}.bin

Each instance uses /Game/Fires/Material/SparseVolumeMaterial as parent, sets the
SparseVolumeTexture parameter and enables Density Mask (R), like the team's setup.
Existing assets are kept and re-configured; the script is safe to run again.
"""
import os
import unreal

DATA_ROOT = r"C:\CodexWork\FireData_it_test"
DEST = "/Game/Fires/FirePrototype/VDB"
PARENT = "/Game/Fires/Material/SparseVolumeMaterial"
SVT_PARAM = "SparseVolumeTexture"
DENSITY_MASK = "Density Mask"
START_PIE = True

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[YUFSFireImport] " + msg)


def import_svt(index):
    name = "fire_it_test_%d" % index
    path = "%s/%s" % (DEST, name)
    if eal.does_asset_exist(path):
        log("%s already exists, keeping it" % path)
        return eal.load_asset(path)
    source = os.path.join(DATA_ROOT, "vdb_%d" % index, "fire_it_test_%d_0001.vdb" % index)
    if not os.path.exists(source):
        unreal.log_error("[YUFSFireImport] missing source " + source)
        return None
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = False
    task.save = True
    log("importing %s (997-frame sequence, this can take a while)" % source)
    asset_tools.import_asset_tasks([task])
    imported = list(task.imported_object_paths or [])
    log("imported: %s" % imported)
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    for p in imported:
        obj = eal.load_asset(p)
        if obj and "SparseVolumeTexture" in obj.get_class().get_name():
            return obj
    unreal.log_error("[YUFSFireImport] import produced no sparse volume texture for " + name)
    return None


def make_mic(index, svt):
    name = "fire_it_test_%d_MIC" % index
    path = "%s/%s" % (DEST, name)
    parent = eal.load_asset(PARENT)
    if not parent:
        unreal.log_error("[YUFSFireImport] parent material missing: " + PARENT)
        return
    mic = eal.load_asset(path) if eal.does_asset_exist(path) else asset_tools.create_asset(
        name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mic, parent)
    ok = False
    setter = getattr(mel, "set_material_instance_sparse_volume_texture_parameter_value", None)
    if setter:
        ok = bool(setter(mic, SVT_PARAM, svt))
    if not ok:
        try:
            value = unreal.SparseVolumeTextureParameterValue()
            info = unreal.MaterialParameterInfo()
            info.set_editor_property("name", SVT_PARAM)
            value.set_editor_property("parameter_info", info)
            value.set_editor_property("parameter_value", svt)
            mic.set_editor_property("sparse_volume_texture_parameter_values", [value])
            ok = True
        except Exception as error:  # noqa: BLE001 - editor API differs by engine version
            unreal.log_error("[YUFSFireImport] could not set %s on %s: %s" % (SVT_PARAM, name, error))
    mel.update_material_instance(mic)
    mask_ok = False
    helper = getattr(unreal, "YUFSFireAssetLibrary", None)
    if helper:
        mask_ok = bool(helper.set_static_component_mask_parameter(mic, DENSITY_MASK, True, False, False, False))
    if not mask_ok:
        unreal.log_error("[YUFSFireImport] could not enable %s (R) on %s - tick it manually" % (DENSITY_MASK, name))
    eal.save_asset(path, only_if_is_dirty=False)
    log("%s -> parent=%s svt=%s set=%s densityMaskR=%s" % (path, PARENT, svt.get_path_name() if svt else None, ok, mask_ok))


def main():
    for index in (1, 2, 3):
        svt = import_svt(index)
        if svt:
            make_mic(index, svt)
    log("done")
    if START_PIE:
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        level_editor.load_level("/Game/Maps/Main")
        level_editor.editor_request_begin_play()
        log("Play-In-Editor requested")


main()
