"""Cut a 200x225cm eastern exit through four overlapping source wall layers.
Original imported assets are preserved; actors reference duplicated cut meshes.
Requires -EnablePlugins=GeometryScripting for this authoring invocation only.
"""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/Maps/it관_찐최종/Main_level')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
library=unreal.GeometryScript_AssetUtils
source_labels={'YUFS_벽막음','YUFS_양면보강','구성요소_96','구성요소_98'}
for index,actor in enumerate(a for a in actors.get_all_level_actors() if a.get_actor_label() in source_labels):
    component=actor.static_mesh_component
    source=component.static_mesh
    path='/Game/YUFS/MergeFinal/SM_EastExitWall_'+str(index)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        # Idempotent: a prior saved cut is reused, never overwrite the imported source.
        component.set_static_mesh(unreal.load_asset(path));continue
    target=unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name().split('.')[0],path)
    dyn=unreal.DynamicMesh()
    dyn,outcome=library.copy_mesh_from_static_mesh(source,dyn,unreal.GeometryScriptCopyMeshFromAssetOptions(),unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.SOURCE_MODEL,lod_index=0))
    assert outcome==unreal.GeometryScriptOutcomePins.SUCCESS
    tool=unreal.DynamicMesh()
    center=unreal.MathLibrary.inverse_transform_location(component.get_world_transform(),unreal.Vector(9583.824,-8673.75,127.5))
    unreal.GeometryScript_Primitives.append_box(tool,unreal.GeometryScriptPrimitiveOptions(),unreal.Transform(location=center),100,200,235,0,0,0,unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    dyn=unreal.GeometryScript_MeshBooleans.apply_mesh_boolean(dyn,unreal.Transform(),tool,unreal.Transform(),unreal.GeometryScriptBooleanOperation.SUBTRACT,unreal.GeometryScriptMeshBooleanOptions())
    options=unreal.GeometryScriptCopyMeshToAssetOptions()
    options.set_editor_property('replace_materials',False)
    _,outcome=library.copy_mesh_to_static_mesh(dyn,target,options,unreal.GeometryScriptMeshWriteLOD(write_hi_res_source=False,lod_index=0))
    assert outcome==unreal.GeometryScriptOutcomePins.SUCCESS
    target.get_editor_property('body_setup').set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    assert unreal.EditorAssetLibrary.save_loaded_asset(target)
    component.set_static_mesh(target)
    unreal.log('[MERGE_FINAL_EAST] cut %s -> %s'%(actor.get_actor_label(),path))
assert level.save_current_level()
unreal.log('[MERGE_FINAL_EAST] saved; navigation rebuild required')
