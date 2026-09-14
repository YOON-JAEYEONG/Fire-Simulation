"""Unreal 5.7 JJW merge asset QA, for a fresh unattended editor process only.

Example (run AFTER building the merged C++ module; do not use -DisablePython):
  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
      -unattended -nop4 -nosplash -NullRHI

No actor spawning, property assignment, asset/map save, redirector fixup, or PIE.
Blueprint compilation is transient: UE 5.7 BlueprintEditorLibrary.cpp explicitly
uses EBlueprintCompileOptions::SkipSave. An asset-file metadata inventory before
and after the audit additionally reports any unexpected persisted changes.
Blueprint.Status is protected in Python; submitting a compile and obtaining a
generated class is NOT a verified compiler result. For strict error counts run
CompileAllBlueprints separately with -AllowListFile=Docs/JJW_MERGE_BLUEPRINT_ALLOWLIST.txt
and a process-local EditorPerProjectUserSettings override disabling SaveOnCompile.
This checks editor loading, reflected components, compilation and exact animation
skeleton contracts. It does NOT certify gameplay, rendering, FDS alignment, or
unloaded World Partition actors. Every result is prefixed [JJW_MERGE_QA].
"""

import collections
import json
import os
import traceback

import unreal


PREFIX = "[JJW_MERGE_QA] "
MAP = "/Game/Maps/Prototype"
BLUEPRINTS = (
    "/Game/Blueprint/BP_YUFSRLEvacuationNPC",
    "/Game/Blueprint/BP_YUFSEvacuationNPC",
    "/Game/Blueprint/BP_NPCPlacementPreview",
    "/Game/Blueprint/UI/WBP_SimHUD",
    "/Game/Blueprint/UI/WBP_SimulationUI",
    "/Game/Blueprint/UI/WBP_NPCAction",
    "/Game/Blueprint/UI/WBP_NPCPaletteEntry",
    "/Game/Blueprint/UI/WBP_NPCRotation",
    "/Game/Blueprint/UI/WBP_RotationDragHandle",
    "/Game/Blueprint/UI/WNP_DropZone",
    "/Game/Blueprint/UI/WNP_NPCPaletteWidget",
)
REQUIRED_COMPONENTS = (
    "YUFSNPCPerceptionComponent", "YUFSBehaviorStateMachine",
    "YUFSSmokeAwareNavigator", "YUFSSocialInfluenceComponent",
    "YUFSNPCDebugComponent", "YUFSLocalMovementComponent",
    "YUFSBeliefComponent", "YUFSIntentComponent", "YUFSActionTaskComponent",
    "YUFSActionAnimationComponent", "YUFSHumanCognitionComponent",
    "YUFSHumanBehaviorSelectorComponent", "YUFSTeamIntegrationComponent",
    "YUFSNpcSuppressionComponent", "YUFSNpcEnvironmentInteraction",
)
# These are the exact native ActionAnimationComponent default soft references.
ANIMATIONS = (
    "/Game/NPCs/Idle", "/Game/NPCs/Fast_Run", "/Game/NPCs/Walking",
    "/Game/NPCs/Standing_Cough_Combined_1",
    "/Game/NPCs/Standing_Using_Touchscreen_Tablet", "/Game/NPCs/Talking",
    "/Game/NPCs/Crawling__1__Anim", "/Game/NPCs/Dying",
)
RESULTS = []
SKELETON_CACHE = {}


def record(status, check, **details):
    row = {"status": status, "check": check}
    row.update(details)
    RESULTS.append(row)
    # Keep even failed individual checks machine-readable. A single final
    # exception marks the Python commandlet as failed after all checks ran.
    unreal.log(PREFIX + json.dumps(row, ensure_ascii=False, sort_keys=True))


def object_path(value):
    return value.get_path_name() if value else "None"


def file_inventory():
    """Read metadata only; never modify packages or hash gigabytes of SVT data."""
    content = os.path.abspath(unreal.Paths.project_content_dir())
    result = {}
    for directory, _, names in os.walk(content):
        for name in names:
            if not name.lower().endswith((".uasset", ".umap")):
                continue
            path = os.path.join(directory, name)
            stat = os.stat(path)
            result[os.path.relpath(path, content)] = (stat.st_size, stat.st_mtime_ns)
    return result


def compile_blueprint(path):
    asset = unreal.load_asset(path)
    if not asset:
        record("FAIL", "blueprint_load", asset=path, reason="missing_or_failed_load")
        return None
    if not isinstance(asset, unreal.Blueprint):
        record("FAIL", "blueprint_type", asset=path, actual=asset.get_class().get_name())
        return None
    # UE 5.7 implementation: FKismetEditorUtilities::CompileBlueprint(..., SkipSave).
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    generated = unreal.BlueprintEditorLibrary.generated_class(asset)
    # Status is protected by the supported Python API. Do not bypass visibility,
    # and do not treat a possibly pre-existing generated class as compile success.
    record("INCOMPLETE" if generated else "FAIL", "blueprint_compile_submitted", asset=path,
           generated_class=object_path(generated), compiler_result_verified=False,
           strict_verification="separate CompileAllBlueprints commandlet error count and exit code")
    return asset if generated else None


def load_animation_skeletons():
    for path in ANIMATIONS:
        asset = unreal.load_asset(path)
        if not asset:
            record("FAIL", "animation_load", asset=path)
            SKELETON_CACHE[path] = None
            continue
        skeleton = asset.get_editor_property("skeleton")
        SKELETON_CACHE[path] = object_path(skeleton) if skeleton else None
        record("PASS" if skeleton else "FAIL", "animation_skeleton", asset=path,
               skeleton=object_path(skeleton))


def inspect_npc(npc, scope):
    components = list(npc.get_components_by_class(unreal.ActorComponent))
    counts = collections.Counter(component.get_class().get_name() for component in components)
    missing = [name for name in REQUIRED_COMPONENTS if counts[name] == 0]
    duplicates = {name: counts[name] for name in REQUIRED_COMPONENTS if counts[name] > 1}
    record("FAIL" if missing else "WARN" if duplicates else "PASS", "npc_components",
           scope=scope, actor=object_path(npc), total=len(components),
           missing=missing, duplicate_classes=duplicates)
    skeletal_components = list(npc.get_components_by_class(unreal.SkeletalMeshComponent))
    if not skeletal_components:
        record("FAIL", "npc_skeleton", scope=scope, actor=object_path(npc), reason="no_skeletal_mesh_component")
        return
    # Character.Mesh is the native mesh passed to ActionAnimationComponent.
    mesh_component = npc.get_editor_property("mesh")
    mesh = mesh_component.get_skeletal_mesh_asset() if mesh_component else None
    skeleton = mesh.get_editor_property("skeleton") if mesh else None
    if not mesh or not skeleton:
        record("FAIL", "npc_skeleton", scope=scope, actor=object_path(npc),
               mesh=object_path(mesh), skeleton=object_path(skeleton))
        return
    mesh_skeleton = object_path(skeleton)
    mismatches = [path for path, value in SKELETON_CACHE.items() if value != mesh_skeleton]
    action_player = next((component for component in components
                          if component.get_class().get_name() == "YUFSActionAnimationComponent"), None)
    if not action_player:
        record("FAIL", "native_animation_preflight", scope=scope, actor=object_path(npc),
               reason="missing_native_animation_component")
        return
    # Read-only native probe checks the actual effective 11 action bindings plus
    # Crawl/Incapacitated, including custom overrides. It does not Initialize a CDO.
    # False now means Initialize keeps the original driver untouched; the actual
    # Initialize/ApplyAction behavior is separately tested by YUFS.NPC.Animation.JJW.*.
    native_supported = action_player.can_use_native_animations(mesh_component)
    record("PASS" if native_supported else "WARN", "npc_action_skeleton_contract",
           scope=scope, actor=object_path(npc), mesh=object_path(mesh),
           skeleton=mesh_skeleton, default_animation_skeleton_mismatches=mismatches,
           effective_native_bindings_supported=native_supported,
           unsupported_policy="preserve_original_driver_before_native_Initialize_changes_mode",
           initialize_preservation_verified_here=False,
           runtime_regression_suite="YUFS.NPC.Animation.JJW")
    anim_class = mesh_component.get_editor_property("anim_class")
    if anim_class:
        result = unreal.BlueprintEditorLibrary.get_blueprint_for_class(anim_class)
        anim_bp = result[0] if isinstance(result, tuple) else result
        if isinstance(anim_bp, unreal.AnimBlueprint):
            target = anim_bp.get_editor_property("target_skeleton")
            # When native playback is unsupported, this is the original driver
            # being preserved, so a mismatched fallback skeleton is still a failure.
            record("PASS" if object_path(target) == mesh_skeleton else "WARN" if native_supported else "FAIL",
                   "configured_anim_blueprint_skeleton", scope=scope,
                   actor=object_path(npc), anim_class=object_path(anim_class),
                   target_skeleton=object_path(target), native_single_node_driver=native_supported)


def run_audit():
    dirty_maps = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if dirty_maps:
        record("FAIL", "fresh_editor_guard", dirty_maps=[object_path(package) for package in dirty_maps],
               reason="refusing_to_replace_an_unsaved_editor_map")
        return
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level(MAP):
        record("FAIL", "map_load", asset=MAP)
        return
    record("PASS", "map_load", asset=MAP)

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Game/Blueprint/UI", "/Game/Maps"], False)
    # Include any additional UI assets added to the incoming branch.
    discovered_ui = registry.get_assets_by_path("/Game/Blueprint/UI", recursive=True)
    paths = list(BLUEPRINTS)
    for data in discovered_ui:
        path = str(data.package_name)
        if path not in paths:
            paths.append(path)
    loaded = {}
    for path in paths:
        try:
            loaded[path] = compile_blueprint(path)
        except Exception as error:
            record("FAIL", "blueprint_exception", asset=path, error=str(error))

    load_animation_skeletons()
    native_class = unreal.load_class(None, "/Script/YUFS.YUFSEvacuationNPC")
    if not native_class:
        record("FAIL", "native_npc_class", reason="merged_module_not_loaded")
        return
    for path in BLUEPRINTS[:2]:
        blueprint = loaded.get(path)
        if not blueprint:
            continue
        try:
            generated = unreal.BlueprintEditorLibrary.generated_class(blueprint)
            default = unreal.get_default_object(generated)
            if not unreal.MathLibrary.class_is_child_of(default.get_class(), native_class):
                record("FAIL", "npc_parent_class", asset=path, actual=object_path(generated))
            else:
                inspect_npc(default, "blueprint_default")
        except Exception as error:
            record("FAIL", "npc_default_exception", asset=path, error=str(error))

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    npcs = [actor for actor in actors if unreal.MathLibrary.class_is_child_of(actor.get_class(), native_class)]
    record("PASS" if npcs else "INCOMPLETE", "loaded_map_npc_inventory", map=MAP,
           loaded_actors=len(actors), loaded_npcs=len(npcs),
           scope="loaded editor actors only; no World Partition load/spawn/PIE was requested")
    for npc in npcs:
        try:
            inspect_npc(npc, "loaded_map_actor")
        except Exception as error:
            record("FAIL", "map_npc_exception", actor=object_path(npc), error=str(error))

    scenarios = [str(data.package_name) for data in registry.get_assets_by_path("/Game", recursive=True)
                 if "scenario04" in str(data.package_name).lower().replace("_", "")]
    record("PASS" if scenarios else "WARN", "scenario04_presence", assets=scenarios,
           note="absence is reported, not replaced with an invented asset or map")


before = file_inventory()
try:
    run_audit()
except Exception as error:
    record("FAIL", "audit_exception", error=str(error), traceback=traceback.format_exc())
finally:
    after = file_inventory()
    changed = sorted(path for path in set(before) | set(after) if before.get(path) != after.get(path))
    record("FAIL" if changed else "PASS", "no_persisted_asset_changes", changed=changed,
           inspected_files=len(before), validation="file size and mtime_ns; no save APIs called")
    counts = collections.Counter(row["status"] for row in RESULTS)
    overall = "FAIL" if counts["FAIL"] else "INCOMPLETE" if counts["INCOMPLETE"] else "PASS_WITH_WARNINGS" if counts["WARN"] else "PASS"
    unreal.log(PREFIX + json.dumps({"check": "SUMMARY", "result": overall,
                                   "counts": dict(counts), "no_gameplay_or_visual_certification": True}, sort_keys=True))
if any(row["status"] == "FAIL" for row in RESULTS):
    raise RuntimeError("JJW merge asset QA failed; see [JJW_MERGE_QA] SUMMARY and individual failures.")
