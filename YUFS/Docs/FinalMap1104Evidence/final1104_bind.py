import unreal,json,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json');r={}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def v(o):return [o.x,o.y,o.z]
def geometry():
    return {a.get_name():[a.static_mesh_component.static_mesh.get_path_name(),v(a.get_actor_location()),[a.get_actor_rotation().pitch,a.get_actor_rotation().yaw,a.get_actor_rotation().roll],v(a.get_actor_scale3d()),str(a.static_mesh_component.get_collision_enabled())] for a in actors.get_all_level_actors() if isinstance(a,unreal.StaticMeshActor) and a.static_mesh_component.static_mesh}
try:
    assert level.load_level('/Game/Maps/it관_찐최종/Main_level')
    before=geometry();all_actors=actors.get_all_level_actors()
    sim=next(a for a in all_actors if isinstance(a,unreal.YUFSSimulationController))
    volume=next(a for a in all_actors if isinstance(a,unreal.YUFSHeterogeneousVolume))
    bm=next(a for a in all_actors if isinstance(a,unreal.YUFSBinaryManager))
    options=list(sim.get_editor_property('fire_options'));assert len(options)==3
    r['before_options']=[str(o) for o in options]
    # OpenVDB's supplied index->source transform is (+.4,+.4,-.4).
    # UE 5.7 adds bounds-min times signed scale (without rotation), giving
    # SVT translation (-21.2,69.2,-2). Undo that origin sign in the actor.
    # Final source coordinates in cm are then (40*x,-40*y,40*z).
    pose=unreal.Transform(location=unreal.Vector(4240,0,-400),rotation=unreal.Rotator(roll=180),scale=unreal.Vector(100,100,100))
    # Empirical fit on supplied density grids: source VDB index =
    # 1.25*BIN index+(55,175,-2); SVT bounds-min=(53,173,-5).
    mapping=unreal.Transform(location=unreal.Vector(2,2,3),scale=unreal.Vector(1.25,1.25,1.25))
    r['new_options']=[]
    for i,o in enumerate(options,1):
        mat=unreal.load_asset(f'/Game/Fires/FinalIT1104/MI_Fire{i}');assert mat
        o.set_editor_property('binary_file_path',f'Fires/FinalIT1104/BinaryData/smoke_data_{i}.bin')
        o.set_editor_property('fire_material',mat);o.set_editor_property('volume_transform',pose)
        o.set_editor_property('use_sparse_volume_texture_coordinates',True)
        o.set_editor_property('binary_grid_to_texture',mapping)
        o.set_editor_property('binary_frames_per_visual_frame',1.)
        o.set_editor_property('binary_frame_offset',0)
        o.set_editor_property('dataset_alignment_confirmed',False)
        options[i-1]=o;r['new_options'].append(str(o))
    sim.set_editor_property('fire_options',options);sim.set_editor_property('auto_apply_fire_volume_transform',True)
    volume.set_actor_transform(pose,False,True)
    volume.get_component_by_class(unreal.HeterogeneousVolumeComponent).set_material(0,unreal.load_asset('/Game/Fires/FinalIT1104/MI_Fire1'))
    volume.set_editor_property('total_frame_count',995.)
    bm.set_editor_property('binary_file_path','Fires/FinalIT1104/BinaryData/smoke_data_1.bin')
    assert before==geometry(),'Geometry or collision changed'
    assert level.save_current_level()
    r.update({'complete':True,'unchanged_meshes':len(before),'bin_grid_world_origin':[2200,-7000,-80],'bin_grid_step_cm':[50,-50,50],'mapping_evidence':'Source VDB transforms plus 9 density-field registrations; empirical ~voxel accuracy. Source physical units and exact export timing remain unconfirmed; provenance gate stays false. Offset0 preserves matching zero-based frame indices, not inferred solely from file counts.'})
except Exception:r['error']=traceback.format_exc()
out.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()
