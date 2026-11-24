"""Place or verify authored visual belongings in the current final IT map.

Run in the Unreal Editor via -ExecutePythonScript. For a two-process workflow,
set YUFS_BELONGINGS_SURVEY_ONLY=1 in headless PIE, then YUFS_BELONGINGS_USE_SURVEY=1
in the graphics editor to author the measured layout. YUFS_BELONGINGS_VERIFY=1
checks a saved map without changing its actors or assets.
"""

import json
import os
import time
import traceback
from pathlib import Path

import unreal as ue

MAP = '/Game/Maps/it관_찐최종/Main_level'
FOLDER = '/Game/Props/Belongings'
TAG = 'YUFSFinalITBelongings'
ANCHORS = {1: (0, 3, 9, 12), 2: (14, 18, 24, 28)}
COLORS = {
    'MI_BagOcean': (.028, .23, .29, 1),
    'MI_BagRust': (.42, .077, .032, 1),
    'MI_BagNavy': (.035, .085, .27, 1),
    'MI_BagMoss': (.11, .24, .085, 1),
    'MI_Trim': (.012, .018, .024, 1),
    'MI_Label': (.60, .64, .66, 1),
    'MI_Zip': (.83, .29, .055, 1),
    'MI_Paper': (.70, .64, .48, 1),
}


def install_palette():
    lib = ue.EditorAssetLibrary
    lib.make_directory(FOLDER)
    tools = ue.AssetToolsHelpers.get_asset_tools()
    base_path = FOLDER + '/M_BelongingsSurface'
    mel = ue.MaterialEditingLibrary
    if lib.does_asset_exist(base_path):
        base = ue.load_asset(base_path)
    else:
        base = tools.create_asset('M_BelongingsSurface',FOLDER,ue.Material,ue.MaterialFactoryNew())
        tint = mel.create_material_expression(base,ue.MaterialExpressionVectorParameter,-260,0)
        tint.set_editor_property('parameter_name','Tint')
        tint.set_editor_property('default_value',ue.LinearColor(.08,.22,.26,1))
        assert mel.connect_material_property(tint,'',ue.MaterialProperty.MP_BASE_COLOR)
        rough = mel.create_material_expression(base,ue.MaterialExpressionConstant,-260,150)
        rough.set_editor_property('r',.81)
        assert mel.connect_material_property(rough,'',ue.MaterialProperty.MP_ROUGHNESS)
        mel.recompile_material(base)
        assert lib.save_loaded_asset(base)
    materials = {}
    for name, color in COLORS.items():
        path = FOLDER + '/' + name
        mat = (ue.load_asset(path) if lib.does_asset_exist(path) else
               tools.create_asset(name, FOLDER, ue.MaterialInstanceConstant, ue.MaterialInstanceConstantFactoryNew()))
        assert mat, path
        mel.set_material_instance_parent(mat, base)
        mel.set_material_instance_vector_parameter_value(mat,'Tint',ue.LinearColor(*color))
        actual = mel.get_material_instance_vector_parameter_value(mat,'Tint')
        assert max(abs(a-b) for a,b in zip((actual.r,actual.g,actual.b),color)) < .001, name
        assert lib.save_loaded_asset(mat), path
        materials[name] = mat
    return materials


def trace(world, start, end):
    return ue.SystemLibrary.line_trace_single(world, start, end,
        ue.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], ue.DrawDebugTrace.NONE, True)


def contact(hit):
    try:
        return hit.get_editor_property('impact_point'), hit.get_editor_property('impact_normal')
    except Exception:
        result = hit.to_tuple()
        return result[5], result[7]


def support(world, point, reference_z):
    center = None
    for x, y in ((0, 0), (19, 0), (-19, 0), (0, 19), (0, -19)):
        ray = point + ue.Vector(x, y, 0)
        hit = trace(world, ray + ue.Vector(0, 0, 36), ray - ue.Vector(0, 0, 55))
        if not hit:
            return None
        contact_point, normal = contact(hit)
        if normal.z < .85 or abs(contact_point.z - reference_z) > 36:
            return None
        if center is None:
            center = contact_point
        elif abs(center.z - contact_point.z) > 4:
            return None
    return center


def room_spot(world, entry):
    side = -1 if entry['side'] == 'North' else 1
    door = ue.Vector((entry['x0']+entry['x1'])/2, entry['y'], entry['floor'])
    # Beyond the door swing and well inside a room, with support for the nearby notebook too.
    reasons = []
    for depth in (360, 320, 285, 250, 225):
        for lateral in (66, -66, 34, -34, 0):
            bag = door + ue.Vector(lateral, side*depth, 0)
            book = bag + ue.Vector(-50 if lateral >= 0 else 50, side*20, 0)
            approach = door + ue.Vector(0, side*165, 32)
            a, b = support(world, bag, door.z), support(world, book, door.z)
            if a is None or b is None:
                raw = trace(world, bag+ue.Vector(0,0,45),bag-ue.Vector(0,0,65))
                reasons.append((depth,lateral,'floor',str(contact(raw)) if raw else None))
                continue
            if any(trace(world, approach, p+ue.Vector(0, 0, 26)) for p in (a, b)):
                reasons.append((depth,lateral,'dividing wall'))
                continue
            if any(trace(world, p+ue.Vector(0, 0, 5), p+ue.Vector(0, 0, 48)) for p in (a, b)):
                reasons.append((depth,lateral,'blocked height'))
                continue
            return a + ue.Vector(0,0,.7), b + ue.Vector(0,0,.7)
    raise RuntimeError('No supported room interior for '+str(entry)+' '+str(reasons[:12]))


def apply_layout(level, actors, editor, layout, verify=False):
    cls = ue.load_class(None, '/Script/YUFS.YUFSBelongingsProp')
    tagged = {a.get_actor_label(): a for a in actors.get_all_level_actors() if TAG in [str(t) for t in a.tags]}
    palette = {} if verify else install_palette()
    results = []
    for floor, anchor_ids in ANCHORS.items():
        for index, anchor in enumerate(anchor_ids):
            for role, point in zip(('Backpack','Notebook'), layout[anchor]):
                label = f'YUFS_{floor}F_{role}_{index+1:02d}'
                actor = tagged.get(label)
                if not verify:
                    angle = (40, 145, -35, 115)[index]+(20 if role=='Notebook' else 0)
                    rotation = ue.Rotator(pitch=0, roll=0, yaw=angle)
                    if actor is None:
                        ue.log('[BELONGINGS_MAP] Authoring '+label)
                        actor = actors.spawn_actor_from_class(cls, point, rotation)
                        assert actor, label
                        actor.set_actor_label(label)
                        actor.set_folder_path(f'Belongings/{floor}F')
                        actor.tags = [TAG]
                    else:
                        actor.set_actor_location(point, False, False)
                        actor.set_actor_rotation(rotation, False)
                    material = ('MI_BagOcean','MI_BagRust','MI_BagNavy','MI_BagMoss')[(index+(1 if role=='Notebook' else 0))%4]
                    actor.set_editor_property('kind', ue.YUFSBelongingsPropKind.BACKPACK if role=='Backpack' else ue.YUFSBelongingsPropKind.NOTEBOOK)
                    actor.set_editor_property('fabric_material', palette[material])
                assert actor, 'Missing authored actor '+label
                where = actor.get_actor_location()
                assert (where-point).length()<2, label
                meshes=[m for m in actor.get_components_by_class(ue.StaticMeshComponent) if m.get_editor_property('static_mesh')]
                assert len(meshes)==(8 if role=='Backpack' else 5), label
                assert all(m.get_collision_enabled()==ue.CollisionEnabled.NO_COLLISION for m in meshes), label
                assert all(m.get_material(0).get_path_name().startswith(FOLDER) for m in meshes), label
                results.append(dict(label=label,floor=floor,kind=role,xyz=[round(where.x,2),round(where.y,2),round(where.z,2)]))
    if not verify:
        assert level.save_current_level(), 'Could not save final IT map'
    output = Path(ue.Paths.project_saved_dir())/('VerifiedBelongingsPlacement.json' if verify else 'AuthoredBelongingsPlacement.json')
    output.write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
    ue.log(f'[BELONGINGS_MAP] {"Verified saved" if verify else "Placed and saved"} {len(results)} props; report: {output}')
    if os.environ.get('YUFS_BELONGINGS_KEEP_OPEN') == '1':
        if not verify:
            assert level.load_level('/Game/Maps/Lvl_MainMenu')
            assert level.load_level(MAP)
            apply_layout(level,actors,editor,layout,True)
            return
        look = next(x['xyz'] for x in results if x['label']=='YUFS_2F_Backpack_01')
        target = ue.Vector(*look)
        camera = target+ue.Vector(120,100,105)
        set_camera = getattr(editor,'set_level_viewport_camera_info',ue.EditorLevelLibrary.set_level_viewport_camera_info)
        set_camera(camera,ue.MathLibrary.find_look_at_rotation(camera,target+ue.Vector(0,0,16)))
        if os.environ.get('YUFS_BELONGINGS_CAPTURE')=='1':
            since=time.monotonic()
            ue.EditorPythonScripting.set_keep_python_script_alive(True)

            def capture(_dt):
                if time.monotonic()-since<1.5:
                    return
                ue.unregister_slate_post_tick_callback(capture_handle)
                target_file=(Path(ue.Paths.project_saved_dir())/'FinalITBelongingsPreview.png').resolve().as_posix()
                ue.SystemLibrary.execute_console_command(editor.get_editor_world(),'HighResShot 1 filename="'+target_file+'"')

            capture_handle=ue.register_slate_post_tick_callback(capture)
    else:
        ue.SystemLibrary.quit_editor()


def main():
    level = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    editor = ue.get_editor_subsystem(ue.UnrealEditorSubsystem)
    assert not level.is_in_play_in_editor()
    assert level.load_level(MAP), MAP
    survey = Path(ue.Paths.project_saved_dir())/'BelongingsFloorSurvey.json'
    verify = os.environ.get('YUFS_BELONGINGS_VERIFY')=='1'
    if os.environ.get('YUFS_BELONGINGS_USE_SURVEY')=='1':
        layout={int(k): [ue.Vector(*p) for p in points] for k,points in json.loads(survey.read_text(encoding='utf8')).items()}
        apply_layout(level,actors,editor,layout,verify)
        return
    plan = json.loads(Path(__file__).with_name('MergeFinalDoorPlan.json').read_text(encoding='utf8'))
    # The Editor world's floor collision can be unregistered before PIE. Measure
    # support in the physics world, then write only the original editor level.
    measured = {}
    started = time.monotonic()
    seen_at = None
    failure = None
    ue.EditorPythonScripting.set_keep_python_script_alive(True)

    def tick(_dt):
        nonlocal seen_at, failure
        try:
            if measured or failure:
                if level.is_in_play_in_editor():
                    return
                ue.unregister_slate_post_tick_callback(handle)
                if failure or os.environ.get('YUFS_BELONGINGS_SURVEY_ONLY')=='1':
                    ue.SystemLibrary.quit_editor()
                else:
                    apply_layout(level,actors,editor,measured,verify)
                return
            assert time.monotonic()-started<100, 'Could not open PIE measurement world'
            world = editor.get_game_world()
            if not world:
                return
            if seen_at is None:
                seen_at = time.monotonic()
            if time.monotonic()-seen_at<1.0:
                return
            for ids in ANCHORS.values():
                for anchor in ids:
                    measured[anchor] = room_spot(world,plan[anchor])
            survey.write_text(json.dumps({k:[[p.x,p.y,p.z] for p in points] for k,points in measured.items()},
                indent=2),encoding='utf8')
            ue.log('[BELONGINGS_MAP] Surveyed two floors in live physics')
            level.editor_request_end_play()
        except Exception:
            failure=traceback.format_exc()
            ue.log_error('[BELONGINGS_MAP] '+failure)
            if level.is_in_play_in_editor():
                level.editor_request_end_play()
            else:
                ue.SystemLibrary.quit_editor()

    handle = ue.register_slate_post_tick_callback(tick)
    level.editor_request_begin_play()


if __name__ == '__main__':
    main()
