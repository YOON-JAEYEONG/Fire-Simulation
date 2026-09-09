"""Import the tracked CC0 OpenGameArt fire extinguisher into Unreal."""

from pathlib import Path

import unreal


DESTINATION_PATH = "/Game/Props/FireExtinguisher/OpenGameArt_JamesWhite_2015"


def import_fire_extinguisher():
    project_dir = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    source_file = (
        project_dir
        / "SourceAssets"
        / "FireExtinguisher"
        / "OpenGameArt_JamesWhite_2015"
        / "fire_extinguisher_model.obj"
    ).resolve()

    if not source_file.is_file():
        raise RuntimeError(f"Source model was not found: {source_file}")

    task = unreal.AssetImportTask()
    task.filename = str(source_file)
    task.destination_path = DESTINATION_PATH
    task.destination_name = "SM_FireExtinguisher_OpenGameArt"
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = True

    unreal.log(f"[YUFSAssetImport] Importing {source_file} -> {DESTINATION_PATH}")
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported_paths = list(task.imported_object_paths)
    if not imported_paths:
        raise RuntimeError("Unreal did not report any imported object paths")

    for object_path in imported_paths:
        asset = unreal.EditorAssetLibrary.load_asset(object_path)
        if isinstance(asset, unreal.StaticMesh):
            try:
                bounds = asset.get_bounds()
                unreal.log(
                    "[YUFSAssetImport] Static mesh "
                    f"{object_path}: origin={bounds.origin}, extent={bounds.box_extent}"
                )
            except Exception as error:
                unreal.log_warning(
                    f"[YUFSAssetImport] Could not inspect {object_path}: {error}"
                )

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log(f"[YUFSAssetImport] Imported objects: {imported_paths}")


import_fire_extinguisher()
