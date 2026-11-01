"""Read-only inventory of the merge_final building; does not save levels/assets."""
import json
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = {}
def vec(v): return [v.x,v.y,v.z]
for package in ['/Game/Maps/Main', '/Game/Maps/it관_찐최종/Main_level']:
    if not level.load_level(package): raise RuntimeError('Cannot load '+package)
    entries = []
    for actor in actors.get_all_level_actors():
        center, extent = actor.get_actor_bounds(False)
        mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
        record = dict(name=actor.get_name(), label=actor.get_actor_label(), kind=actor.get_class().get_name(),
            position=vec(actor.get_actor_location()), rotation=str(actor.get_actor_rotation()),
            scale=vec(actor.get_actor_scale3d()), center=vec(center), extent=vec(extent))
        if mesh:
            record.update(mesh=mesh.static_mesh.get_path_name() if mesh.static_mesh else None,
                collision=str(mesh.get_collision_enabled()), profile=str(mesh.get_collision_profile_name()),
                visible=mesh.is_visible())
        entries.append(record)
    report[package]=entries
    unreal.log('[MERGE_FINAL_AUDIT] %s actors=%d' % (package,len(entries)))
out=Path(unreal.Paths.project_saved_dir())/'MergeFinalMapAudit.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('[MERGE_FINAL_AUDIT] inventory='+str(out))
