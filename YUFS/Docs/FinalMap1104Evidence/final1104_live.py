import unreal,json,time,traceback,re
from pathlib import Path
out=Path(__file__).with_suffix('.json');r={'mode':'Actual final-map rendered PIE. 12 real palette NPCs, default personalities/preparation/exposure/everyday behavior; rule policy. All scheduled communications disabled only for this test to prove smoke/heat-driven decisions. Fire option3 progresses naturally from frame0 at saved playback fps. No assets saved.','samples':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);ed=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/it관_찐최종/Main_level');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
stage='ready';ready=None;done=False;started=time.monotonic();last=-1;npcs=[]
def f(o,k):return o.get_editor_property(k)
def vec(v):return [v.x,v.y,v.z]
def save():out.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
def sample():
 row={'time':sim.get_elapsed_time(),'frame':volume.get_frame(),'phase':str(sim.get_current_phase()),'evacuated':sim.get_evacuated_count(),'incap':sim.get_incapacitated_count(),'npcs':[]}
 for n in npcs:
  sm=n.get_component_by_class(unreal.YUFSBehaviorStateMachine)
  row['npcs'].append({'name':n.get_name(),'xyz':vec(n.get_actor_location()),'hidden':f(n,'hidden'),'audit':n.get_hazard_audit(),'cue':str(sm.get_decision_cue()),'traffic':n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_traffic_diagnostics()})
 r['samples'].append(row);save();return row
def tick(dt):
 global stage,ready,done,last,sim,volume
 try:
  if done:
   if not level.is_in_play_in_editor():
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
   return
  if time.monotonic()-started>200:raise RuntimeError('Natural smoke run timeout')
  w=ed.get_game_world()
  if not w:return
  if ready is None:ready=time.monotonic();return
  if time.monotonic()-ready<1:return
  if stage=='ready':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   cls=f(f(unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.YUFSNPCPaletteWidget,False)[0],'available_np_cs')[0],'npc_class')
   for i,(x,y,z) in enumerate([(3000,-8660,20),(4500,-8650,20),(6000,-8650,20),(7800,-8650,20),(9000,-8650,20),(3100,-8150,20),(3000,-8660,370),(4500,-8650,370),(6000,-8650,370),(7800,-8650,370),(9000,-8650,370),(8800,-8150,370)]):
    p=unreal.YUFSMapDiagnostics.probe_location(w,unreal.Vector(x+13,y+7,z),cls)
    assert f(p,'bSupported') and f(p,'bBodyClear') and f(p,'bOnNavigation'),str(p)
    tr=unreal.Transform(location=f(p,'NavigationLocation')+unreal.Vector(0,0,90));api=unreal.get_default_object(unreal.GameplayStatics)
    n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));assert n
    n.set_editor_property('data_collection_mode',True);n.set_editor_property('StableNPCId',4000+i)
    api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));npcs.append(n)
   sim.select_fire_option(2)
   for k in ('alarm_trigger_offset_seconds','pre_recorded_msg_offset_seconds','live_announcement_offset_seconds','staff_guidance_offset_seconds'):
    sim.set_editor_property(k,-1.)
   sim.set_editor_property('fire_start_delay_seconds',0.);sim.set_editor_property('timeline_record_end_fire_seconds',60.)
   r['initial']=[vec(n.get_actor_location()) for n in npcs];sample();sim.start_simulation();stage='run'
  elif stage=='run':
   if int(sim.get_elapsed_time())!=last:last=int(sim.get_elapsed_time());sample()
   if sim.is_timeline_review_mode():
    row=sample();r['result']={'evacuated':row['evacuated'],'incap':row['incap'],'total':sim.get_total_npc_count()}
    assert all('alarm=0' in n['audit'] for s in r['samples'] for n in s['npcs']), 'Test unexpectedly received an alarm'
    cues={n['cue'] for s in r['samples'] for n in s['npcs']}
    assert any('SMOKE' in x.upper() or 'HEAT' in x.upper() for x in cues),str(cues)
    assert any('state=Evacuating' in n['audit'] for s in r['samples'] for n in s['npcs'])
    moved=sum(sum((a-b)**2 for a,b in zip(n['xyz'],r['initial'][i]))>10000 for i,n in enumerate(row['npcs']))
    assert moved>0,'No actor moved after actual smoke evidence'
    r['moved_more_than_100cm']=moved;r['decision_cues']=sorted(cues);r['complete']=True;save();done=True;level.editor_request_end_play()
 except Exception:r['error']=traceback.format_exc();save();done=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
