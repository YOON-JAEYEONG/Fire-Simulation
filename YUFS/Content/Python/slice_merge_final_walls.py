"""Extract horizontal wall sections from final-map geometry for placement review."""
import unreal,json
from pathlib import Path
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/Maps/it관_찐최종/Main_level')
records=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    mesh=actor.get_component_by_class(unreal.StaticMeshComponent)
    if not mesh or not mesh.static_mesh or actor.get_actor_label()=='Sree':continue
    desc=mesh.static_mesh.get_static_mesh_description(0)
    if not desc:continue
    transform=mesh.get_world_transform()
    for z in (120,470):
        segments=[]
        for tid in range(desc.get_triangle_count()):
            vertices=desc.get_triangle_vertices(unreal.TriangleID(tid))[-3:]
            pts=[unreal.MathLibrary.transform_location(transform,desc.get_vertex_position(v)) for v in vertices]
            hits=[]
            for a,b in ((pts[0],pts[1]),(pts[1],pts[2]),(pts[2],pts[0])):
                if (a.z-z)*(b.z-z)<0:
                    t=(z-a.z)/(b.z-a.z);hits.append([a.x+t*(b.x-a.x),a.y+t*(b.y-a.y)])
            if len(hits)==2:segments.append(hits)
        if segments:records.append(dict(label=actor.get_actor_label(),z=z,segments=segments))
out=Path(unreal.Paths.project_saved_dir())/'MergeFinalWallSlices.json'
out.write_text(json.dumps(records,ensure_ascii=False),encoding='utf8')
unreal.log('[MERGE_FINAL_SLICE] records=%d output=%s'%(len(records),out))
