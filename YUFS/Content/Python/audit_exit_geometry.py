"""Read-only Main inventory; never save a level or infer passage from a marker."""
import json
import unreal

editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.load_level('/Game/Maps/Main'):
    raise RuntimeError('Main failed to load')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for actor in actors:
    label = actor.get_actor_label()
    kind = actor.get_class().get_name()
    if actor.get_component_by_class(unreal.StaticMeshComponent) or kind == 'YUFSExitPoint' or 'navmesh' in label.lower():
        center, extent = actor.get_actor_bounds(False)
        mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
        record = dict(label=label, kind=kind, position=str(actor.get_actor_location()),
                      rotation=str(actor.get_actor_rotation()), center=str(center), extent=str(extent),
                      mesh=mesh.static_mesh.get_path_name() if mesh and mesh.static_mesh else None)
        if kind == 'YUFSExitPoint':
            record.update(width=actor.get_editor_property('ExitWidth'),
                          marker_floor_offset=actor.get_editor_property('MarkerFloorOffset'),
                          marker_wall_distance=actor.get_editor_property('MarkerWallDistance'))
        unreal.log('[EXIT_GEOMETRY] ' + json.dumps(record, ensure_ascii=False))
unreal.log('[EXIT_GEOMETRY] trace API: ' + str(unreal.SystemLibrary.capsule_trace_single.__doc__))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for y in (-100, 0, 100, 200, 300):
    result = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(2155,y,120), unreal.Vector(2155,y,-60),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    unreal.log('[EXIT_GEOMETRY] center floor y=%s result=%s' % (y, result))
unreal.log('[EXIT_GEOMETRY] complete; no asset or map saved')
