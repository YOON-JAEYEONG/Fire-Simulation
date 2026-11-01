"""Open final building in PIE, add 20 transient occupants, leave Start to the user."""
import json,time,traceback
from pathlib import Path
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level('/Game/Maps/it관_찐최종/Main_level')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started=time.monotonic()
def tick(dt):
    try:
        world=editor.get_game_world()
        if not world:
            if time.monotonic()-started>120:
                unreal.unregister_slate_post_tick_callback(handle)
            return
        existing=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.YUFSEvacuationNPC)
        placed=len(existing)
        if not existing:
            cls=unreal.load_class(None,'/Game/Blueprint/BP_YUFSRLEvacuationNPC.BP_YUFSRLEvacuationNPC_C')
            half=unreal.get_default_object(cls).capsule_component.get_scaled_capsule_half_height()
            # Use both storeys; positions remain inside the imported rooms.
            plan=json.loads((Path(unreal.Paths.project_content_dir())/'Python/MergeFinalDoorPlan.json').read_text(encoding='utf8'))
            selected=[d for d in plan if d['floor']==20][:10]+[d for d in plan if d['floor']==370][:10]
            for d in selected:
                side=-1 if d['side']=='North' else 1
                p=unreal.Vector((d['x0']+d['x1'])/2,d['y']+side*200,d['floor'])
                projected=unreal.NavigationSystemV1.project_point_to_navigation(world,p,None,None,unreal.Vector(60,60,45))
                if projected is None or abs(projected.z-p.z)>40:continue
                tr=unreal.Transform(location=projected+unreal.Vector(0,0,half+2))
                npc=unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(world,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                if npc:
                    unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor',(npc,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));placed+=1
        unreal.log('[MERGE_FINAL_PREVIEW] occupants=%d; user may press Start; free camera unchanged'%placed)
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    except Exception:
        unreal.log_error('[MERGE_FINAL_PREVIEW] '+traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
