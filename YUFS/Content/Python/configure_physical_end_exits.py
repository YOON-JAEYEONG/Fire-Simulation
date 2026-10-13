"""Open the two ground-floor END exits of Main physically, matching the central exit setup.

The imported IT-building wall mesh (actor '벽_0', asset 그룹1) has no opening at the west
(x=0) or east (x=4300) end of the ground-floor corridor (y=-1040..-800, centre y=-920).
This script:

1. Boolean-subtracts a doorway (180 x 220 cm) through each end wall on the corridor axis
   with Geometry Script and writes the result to a NEW static mesh asset
   (/Game/YUFS/Exits/SM_IT_F1_Walls_EndExits). The original Datasmith asset is untouched;
   only the '벽_0' actor now references the cut mesh. Collision is per-triangle
   (complex-as-simple) so the opening is also open for pawns and navmesh generation.
2. Places an exterior simulation apron (walkable floor) outside each new doorway, like the
   one already used by the central exit. The aprons are implementation support surfaces,
   not surveyed site geometry.
3. Moves/configures the existing YUFS_Exit_WestEnd / YUFS_Exit_EastEnd actors onto the
   outer wall face with bPhysicalPassageEnabled=true, portal at the floor, 300 cm exterior
   target and a 400x400 cm waiting spread (same contract as MainEntrance_GroundFloor).
4. Widens the NavMeshBoundsVolume along X so the exterior targets are inside it.

Doorway size (180 x 220 cm) is a working assumption, not a surveyed dimension.
Run headless (needs the GeometryScripting plugin, enabled per run):
  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file>
      -EnablePlugins=GeometryScripting -unattended -nop4 -NullRHI -NoSound
Then bake navigation with rebuild_exit_navigation.py and validate in PIE.
"""
import json
import unreal

MAP = '/Game/Maps/Main'
WALL_LABEL = '벽_0'
SOURCE_WALL_MESH = '/Game/Maps/it관_완성/Geometries/그룹1'
NEW_MESH_FOLDER = '/Game/YUFS/Exits'
NEW_MESH_NAME = 'SM_IT_F1_Walls_EndExits'
NEW_MESH_PATH = NEW_MESH_FOLDER + '/' + NEW_MESH_NAME
CORRIDOR_Y = -920.0
OPENING_WIDTH = 180.0
OPENING_HEIGHT = 220.0
EXTERIOR_DISTANCE = 300.0
APRON_HALF = 300.0  # 600 x 600 cm cube top at z=0, same as the central apron
NAV_X_MIN, NAV_X_MAX = -650.0, 4950.0
CENTRAL_LABEL = 'MainEntrance_GroundFloor'

# label, ExitID, outer wall face x, yaw facing OUT, cut box centre x, apron centre x
ENDS = [
    ('YUFS_Exit_WestEnd', 'WestEnd', 0.0, 180.0, 5.0, -APRON_HALF),
    ('YUFS_Exit_EastEnd', 'EastEnd', 4300.0, 0.0, 4295.0, 4300.0 + APRON_HALF),
]
CUT_THICKNESS = 90.0  # covers the 20 cm wall plus margin on both faces


def log(message):
    unreal.log('[PHYSICAL_END_EXIT_SETUP] ' + message)


def new_dynamic_mesh():
    try:
        return unreal.DynamicMesh()
    except Exception:
        return unreal.new_object(unreal.DynamicMesh)


# UE 5.7 exposes the static-mesh copy functions as GeometryScript_AssetUtils (older: GeometryScript_StaticMesh).
ASSET_LIB = getattr(unreal, 'GeometryScript_AssetUtils', None) or getattr(unreal, 'GeometryScript_StaticMesh')


def cut_end_openings(source_mesh):
    """Return a new static mesh asset: source_mesh minus one doorway per end wall."""
    if unreal.EditorAssetLibrary.does_asset_exist(NEW_MESH_PATH):
        assert unreal.EditorAssetLibrary.delete_asset(NEW_MESH_PATH), 'could not replace old cut mesh'
    if not unreal.EditorAssetLibrary.does_directory_exist(NEW_MESH_FOLDER):
        unreal.EditorAssetLibrary.make_directory(NEW_MESH_FOLDER)
    target = unreal.EditorAssetLibrary.duplicate_asset(source_mesh.get_path_name().split('.')[0], NEW_MESH_PATH)
    assert target, 'duplicate_asset failed'

    dyn = new_dynamic_mesh()
    read_lod = unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.SOURCE_MODEL, lod_index=0)
    dyn, outcome = ASSET_LIB.copy_mesh_from_static_mesh(
        source_mesh, dyn, unreal.GeometryScriptCopyMeshFromAssetOptions(), read_lod)
    assert outcome == unreal.GeometryScriptOutcomePins.SUCCESS, 'copy_mesh_from_static_mesh failed'
    tri_before = dyn.get_triangle_count()

    for label, exit_id, face_x, yaw, cut_x, apron_x in ENDS:
        tool = new_dynamic_mesh()
        centre = unreal.Vector(cut_x, CORRIDOR_Y, OPENING_HEIGHT * 0.5 - 5.0)
        tool = unreal.GeometryScript_Primitives.append_box(
            tool, unreal.GeometryScriptPrimitiveOptions(), unreal.Transform(centre, unreal.Rotator(), unreal.Vector(1, 1, 1)),
            CUT_THICKNESS, OPENING_WIDTH, OPENING_HEIGHT + 10.0, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
        dyn = unreal.GeometryScript_MeshBooleans.apply_mesh_boolean(
            dyn, unreal.Transform(), tool, unreal.Transform(),
            unreal.GeometryScriptBooleanOperation.SUBTRACT, unreal.GeometryScriptMeshBooleanOptions())
        log('%s doorway cut at x=%.0f y=%.0f +/-%.0f z=0..%.0f (triangles now %d)' % (
            exit_id, face_x, CORRIDOR_Y, OPENING_WIDTH * 0.5, OPENING_HEIGHT, dyn.get_triangle_count()))
    assert dyn.get_triangle_count() > tri_before, 'boolean produced no new triangles; wall shell may not be closed'

    write_options = unreal.GeometryScriptCopyMeshToAssetOptions()
    for key, value in {'enable_recompute_normals': True, 'enable_recompute_tangents': True,
                       'replace_materials': False, 'emit_transaction': False}.items():
        try:
            write_options.set_editor_property(key, value)
        except Exception as error:
            log('option %s skipped: %s' % (key, error))
    # Returns (DynamicMesh, outcome); the static mesh asset itself is modified in place.
    _, outcome = ASSET_LIB.copy_mesh_to_static_mesh(
        dyn, target, write_options, unreal.GeometryScriptMeshWriteLOD(write_hi_res_source=False, lod_index=0))
    assert outcome == unreal.GeometryScriptOutcomePins.SUCCESS, 'copy_mesh_to_static_mesh failed'

    try:
        body = target.get_editor_property('body_setup')
        body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        log('collision trace flag set to complex-as-simple on the cut mesh')
    except Exception as error:
        log('WARNING could not set collision trace flag: %s' % error)
    assert unreal.EditorAssetLibrary.save_loaded_asset(target), 'could not save cut wall mesh'
    return target


def slice_gaps(static_mesh, face_x, z, band=60.0):
    """Covered y-intervals of wall triangles within `band` cm of the outer face x at height z."""
    desc = static_mesh.get_static_mesh_description(0)
    cover = []
    for t in range(desc.get_triangle_count()):
        tid = unreal.TriangleID(t) if hasattr(unreal, 'TriangleID') else t
        pts = [desc.get_vertex_position(v) for v in desc.get_triangle_vertices(tid)][-3:]
        seg = []
        for a, b in ((pts[0], pts[1]), (pts[1], pts[2]), (pts[2], pts[0])):
            if (a.z - z) * (b.z - z) < 0:
                f = (z - a.z) / (b.z - a.z)
                seg.append((a.x + f * (b.x - a.x), a.y + f * (b.y - a.y)))
        if len(seg) == 2 and abs(seg[0][0] - face_x) <= band and abs(seg[1][0] - face_x) <= band:
            cover.append((min(seg[0][1], seg[1][1]), max(seg[0][1], seg[1][1])))
    gaps, start, y = [], None, -1840.0
    while y <= 0.0:
        covered = any(a - 1 <= y <= b + 1 for a, b in cover)
        if not covered and start is None:
            start = y
        if covered and start is not None:
            gaps.append((start, y)); start = None
        y += 10.0
    if start is not None:
        gaps.append((start, 0.0))
    return [g for g in gaps if g[1] - g[0] >= 60]


def main():
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert editor.load_level(MAP), 'Main failed to load'
    all_actors = actors.get_all_level_actors()

    wall = next(a for a in all_actors if a.get_actor_label() == WALL_LABEL)
    smc = wall.get_component_by_class(unreal.StaticMeshComponent)
    xf = smc.get_world_transform()
    assert xf.translation.is_near_equal(unreal.Vector(0, 0, 0), 0.5) and xf.scale3d.is_near_equal(unreal.Vector(1, 1, 1), 0.001) \
        and xf.rotation.is_identity(0.001), 'wall actor transform is not identity; cut box assumes world == mesh space'
    source = unreal.load_asset(SOURCE_WALL_MESH)
    assert source, 'source wall mesh missing'
    log('wall actor %s currently uses %s' % (WALL_LABEL, smc.static_mesh.get_path_name()))

    cut_mesh = cut_end_openings(source)
    smc.set_static_mesh(cut_mesh)
    smc.set_editor_property('can_ever_affect_navigation', True)
    for face_x, exit_id in ((0.0, 'WestEnd'), (4300.0, 'EastEnd')):
        for z in (30.0, 100.0, 200.0):
            log('verify %s face x=%.0f z=%.0f gaps=%s' % (exit_id, face_x, z, slice_gaps(cut_mesh, face_x, z)))

    exits = [a for a in all_actors if a.get_class().get_name() == 'YUFSExitPoint']
    central = next(a for a in exits if a.get_actor_label() == CENTRAL_LABEL)
    central.set_editor_property('bPhysicalPassageEnabled', True)
    summary = {'central': str(central.get_actor_location())}
    for label, exit_id, face_x, yaw, cut_x, apron_x in ENDS:
        exit_actor = next((a for a in exits if a.get_actor_label() == label), None)
        if exit_actor is None:
            exit_actor = actors.spawn_actor_from_class(unreal.load_class(None, '/Script/YUFS.YUFSExitPoint'),
                                                       unreal.Vector(face_x, CORRIDOR_Y, 0), unreal.Rotator(0, 0, yaw))
            exit_actor.set_actor_label(label)
            exit_actor.set_folder_path('YUFS/Exits')
        exit_actor.set_actor_location(unreal.Vector(face_x, CORRIDOR_Y, 0), False, False)
        exit_actor.set_actor_rotation(unreal.Rotator(0, 0, yaw), False)
        for key, value in {'ExitID': exit_id, 'bPhysicalPassageEnabled': True, 'ExitWidth': OPENING_WIDTH,
                           'PassageHeight': OPENING_HEIGHT, 'ExteriorDistance': EXTERIOR_DISTANCE,
                           'MarkerFloorOffset': 0.0, 'MarkerWallDistance': 0.0,
                           'PortalFloorOffset': unreal.Vector(), 'ExteriorSpread': unreal.Vector2D(400, 400),
                           'bShowDoorLeaf': False, 'bShowMarker': True}.items():
            exit_actor.set_editor_property(key, value)

        apron_label = 'YUFS_%s_SimulationApron' % exit_id
        pad = next((a for a in all_actors if a.get_actor_label() == apron_label), None)
        if pad is None:
            pad = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(apron_x, CORRIDOR_Y, -10))
            pad.set_actor_label(apron_label)
            pad.set_folder_path('YUFS/Exits')
        pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        pad.static_mesh_component.set_collision_profile_name('BlockAll')
        pad.set_actor_location(unreal.Vector(apron_x, CORRIDOR_Y, -10), False, False)
        pad.set_actor_scale3d(unreal.Vector(APRON_HALF / 50.0, APRON_HALF / 50.0, 0.2))
        pad.tags = ['SimulationApron_NotSurveyed']
        summary[exit_id] = dict(exit=str(exit_actor.get_actor_location()), yaw=yaw,
                                nav_target=str(exit_actor.get_navigation_destination()), apron=str(pad.get_actor_location()))

    for actor in all_actors:
        if actor.get_class().get_name() == 'NavMeshBoundsVolume':
            scale = actor.get_actor_scale3d()
            center, extent = actor.get_actor_bounds(False)
            if extent.x > 0:
                needed = max(extent.x, (NAV_X_MAX - NAV_X_MIN) * 0.5)
                location = actor.get_actor_location()
                location.x += (NAV_X_MIN + NAV_X_MAX) * 0.5 - center.x
                actor.set_actor_location(location, False, False)
                actor.set_actor_scale3d(unreal.Vector(scale.x * needed / extent.x, scale.y, scale.z))
                center, extent = actor.get_actor_bounds(False)
                summary['navmesh_bounds_x'] = [center.x - extent.x, center.x + extent.x]
                summary['navmesh_bounds_y'] = [center.y - extent.y, center.y + extent.y]

    assert editor.save_current_level(), 'could not save Main'
    log('summary ' + json.dumps(summary, ensure_ascii=False))
    log('All three ground-floor exits are physical now; rebuild navigation and validate in PIE.')


main()
