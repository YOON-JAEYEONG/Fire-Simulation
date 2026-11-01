"""Transient PIE checks for the final building; never save diagnostic actors/config."""
import json,time,traceback
from pathlib import Path
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level('/Game/Maps/it관_찐최종/Main_level')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started=time.monotonic();stage=0;door=None;npc=None;report={}
def trace(world,a,b,ignore):
    return unreal.SystemLibrary.capsule_trace_single(world,a,b,34,86,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,ignore,unreal.DrawDebugTrace.NONE,True) is not None
def tick(dt):
    global stage,door,npc
    try:
        if stage==2 and not level.is_in_play_in_editor():
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
        world=editor.get_game_world()
        if not world:
            if time.monotonic()-started>80:raise RuntimeError('PIE world unavailable')
            return
        if stage==0:
            doors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.YUFSInteractionDoor)
            exits=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.YUFSExitPoint)
            report.update(map=world.get_name(),doorCount=len(doors),exitCount=len(exits),exits=[])
            assert len(doors)==30 and len(exits)==3
            door=next(d for d in doors if '1F_North_01' in d.get_actor_label())
            transform=door.get_actor_transform()
            center=unreal.MathLibrary.transform_location(transform,unreal.Vector(0,50,90))
            forward=door.get_actor_forward_vector()
            spawn_at=door.get_handle_location()+forward*75-unreal.Vector(0,0,15)
            tr=unreal.Transform(location=spawn_at)
            npc=unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(world,unreal.YUFSEvacuationNPC,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
            unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor',(npc,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
            npc.set_actor_tick_enabled(False)
            npc.character_movement.set_component_tick_enabled(False)
            report['closedDoorBlocks']=trace(world,center+forward*100,center-forward*100,[npc])
            for exit_actor in exits:
                target=exit_actor.get_actor_location()
                inside=target-exit_actor.get_actor_forward_vector()*400
                path=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,inside,target,npc)
                item=dict(id=str(exit_actor.get_editor_property('ExitID')),pathPoints=len(path.path_points) if path else 0,partial=path.is_partial() if path else True,
                    apertureClear=not trace(world,inside+unreal.Vector(0,0,90),target+unreal.Vector(0,0,90),[npc]))
                report['exits'].append(item)
            controller=unreal.GameplayStatics.get_actor_of_class(world,unreal.YUFSSimulationController)
            controller.set_editor_property('FireStartDelaySeconds',0)
            controller.start_simulation()
            assert door.try_use(npc),'Door reservation failed at the handle'
            stage=1;report['openingRequestedAt']=time.monotonic()
        elif stage==1 and (door.is_open() or time.monotonic()-report['openingRequestedAt']>20):
            center=unreal.MathLibrary.transform_location(door.get_actor_transform(),unreal.Vector(0,50,90))
            forward=door.get_actor_forward_vector()
            report['doorOpened']=door.is_open();report['passageClear']=door.is_passage_clear()
            report['openDoorBlocks']=trace(world,center+forward*100,center-forward*100,[npc])
            report['success']=report['closedDoorBlocks'] and report['doorOpened'] and report['passageClear'] and not report['openDoorBlocks'] and all(e['pathPoints']>1 and not e['partial'] and e['apertureClear'] for e in report['exits'])
            stage=2
            unreal.log('[MERGE_FINAL_VERIFY] '+json.dumps(report))
            (Path(unreal.Paths.project_saved_dir())/'MergeFinalVerification.json').write_text(json.dumps(report,indent=2),encoding='utf8')
            level.editor_request_end_play()
        elif stage==2 and not level.is_in_play_in_editor():
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
        if time.monotonic()-started>80:raise RuntimeError('Verification timeout')
    except Exception:
        unreal.log_error('[MERGE_FINAL_VERIFY] '+traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle)
        level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
