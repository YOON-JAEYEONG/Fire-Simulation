import unreal,json,time,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered saved Main PIE: default4 movers/progressing fire; unfamiliar upstairs explorer/fixed0 with pause and explicit nearby guide; ground-floor visual exit discovery. Temporary runtime fixtures only.','runs':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
case=0;stage='setup';stopping=False;ending=False;restart=None;began=time.monotonic();mark=began;last=-1;npcs=[]

def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def vec(v):return [v.x,v.y,v.z]
def mem(n):return n.get_component_by_class(unreal.YUFSExitKnowledgeComponent)
def spawn(w,cls,xyz,visitor=False,early=False,yaw=0):
 tr=unreal.Transform(location=unreal.Vector(*xyz),rotation=unreal.Rotator(pitch=0,yaw=yaw,roll=0));api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 mem(n).set_editor_property('remember_marked_entry',not visitor)
 n.set_editor_property('enable_everyday_behavior',False)
 if early:
  cfg=unreal.YUFSBehaviorConfig();cfg.set_editor_property('early_alarm_response_fraction',1);cfg.set_editor_property('verify_alarm_response_fraction',0);cfg.set_editor_property('preparation_duration',0)
  n.get_component_by_class(unreal.YUFSBehaviorStateMachine).set_editor_property('config',cfg)
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return n
def sample(label):
 row={'label':label,'phase':str(sim.get_current_phase()),'time':sim.get_elapsed_time(),'frame':volume.get_frame(),'evacuated':sim.get_evacuated_count(),'npcs':[]}
 for n in npcs:
  row['npcs'].append({'xyz':vec(n.get_actor_location()),'hidden':n.get_editor_property('hidden'),'known':mem(n).get_known_exit_count(),'searching':mem(n).is_searching_for_exit(),'has_target':mem(n).has_search_target(),'queries':mem(n).get_search_query_count(),'knowledge':mem(n).get_knowledge_diagnostics(),'goal':vec(n.get_component_by_class(unreal.YUFSSmokeAwareNavigator).get_requested_destination()),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name(),'audit':n.get_hazard_audit()})
 report['runs'][-1]['samples'].append(row);save();return row
def stop_run():
 global stopping,ending,restart
 stopping=True;ending=case==2;restart=None;level.editor_request_end_play()

def tick(dt):
 global case,stage,stopping,ending,restart,mark,last,npcs,sim,volume,cls,initial,paused,guide
 try:
  now=time.monotonic()
  if stopping:
   if level.is_in_play_in_editor():return
   if ending:
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
   if restart is None:restart=now;return
   if now-restart<1:return
   case+=1;stage='setup';stopping=False;npcs=[];last=-1;level.editor_request_begin_play();return
  if now-began>150:raise RuntimeError('Probe exceeded150 wall seconds')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget))
   cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
   exits=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.YUFSExitPoint)
   report['runs'].append({'case':case,'exits':[{'id':str(e.get_editor_property('exit_id')),'entry':e.get_editor_property('is_familiar_entry'),'xyz':vec(e.get_actor_location())} for e in exits],'samples':[]})
   if case==0:
    for xyz in [(1100,-300,350),(1400,-450,350),(1700,-650,350),(2000,-500,350)]:
     floor=unreal.NavigationSystemV1.project_point_to_navigation(w,unreal.Vector(*xyz),None,None,unreal.Vector(45,45,45));assert floor is not None
     npcs.append(spawn(w,cls,vec(floor+unreal.Vector(0,0,90))))
    assert all(mem(n).get_known_exit_count()==1 for n in npcs),'Marked Main entry prior is missing'
   elif case==1:
    floor=unreal.NavigationSystemV1.project_point_to_navigation(w,unreal.Vector(1100,-850,350),None,None,unreal.Vector(45,45,45));assert floor is not None
    npcs.append(spawn(w,cls,vec(floor+unreal.Vector(0,0,90)),visitor=True,early=True))
    assert mem(npcs[0]).get_known_exit_count()==0,'Visitor inherited global exit knowledge'
   else:
    npcs.append(spawn(w,cls,(2150,-350,90),visitor=True,early=True,yaw=180))
    mark=now;stage='look_away';sample('spawn_looking_away');return
   initial=[vec(n.get_actor_location()) for n in npcs]
   sim.set_editor_property('fire_start_delay_seconds',0)
   if case==1:sim.set_editor_property('alarm_trigger_offset_seconds',0)
   sim.start_simulation();stage='align';mark=now
  elif stage=='align' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   if case==1:volume.set_frame(0)
   stage='run'
  elif stage=='run':
   elapsed=int(sim.get_elapsed_time())
   if elapsed!=last:last=elapsed;sample('progress')
   if case==0 and sim.get_elapsed_time()>=25:
    row=sample('default_end');distances=[sum((a-b)**2 for a,b in zip(n['xyz'],p))**.5 for n,p in zip(row['npcs'],initial)]
    assert all(d>150 for d in distances),'Default remembered-entry NPC stopped moving'
    report['runs'][-1]['displacement_cm']=distances;save();stop_run()
   elif case==1 and sim.get_elapsed_time()>=2 and mem(npcs[0]).has_search_target():
    assert mem(npcs[0]).get_known_exit_count()==0,'Explorer already sees an exit; fixture no longer isolates unknown search'
    sim.pause_simulation();paused=sample('search_pause_start');stage='paused';mark=now
   elif case==1 and sim.get_elapsed_time()>8:raise RuntimeError('Unknown-exit search never obtained a supported waypoint')
  elif stage=='paused' and now-mark>=2:
   end=sample('search_pause_after2s')
   assert end['time']==paused['time'] and end['frame']==paused['frame']
   for k in ['xyz','goal','queries','known','has_target']:assert end['npcs'][0][k]==paused['npcs'][0][k],'Pause altered '+k
   sim.resume_simulation();volume.set_frame(0);stage='searching';mark=now
  elif stage=='searching' and sim.get_elapsed_time()>=7:
   before=sample('before_explicit_peer_guidance');n=npcs[0]
   assert mem(n).get_known_exit_count()==0,'Visitor discovered an exit before guidance fixture'
   assert sum((a-b)**2 for a,b in zip(vec(n.get_actor_location()),initial[0]))**.5>100,'Explorer did not physically move'
   # Explicit stationary speaker fixture; no collision so it cannot change the explorer's traffic geometry.
   guide=spawn(w,cls,vec(n.get_actor_location()));guide.set_actor_enable_collision(False);guide.set_editor_property('use_external_navigation_driver',True);guide.character_movement.set_component_tick_enabled(False)
   assert mem(guide).get_known_exit_count()==1,'Speaker does not remember the marked entry'
   assert n.receive_exit_guidance(guide),'Nearby known-exit guidance was rejected'
   assert mem(n).get_known_exit_count()==1 and not mem(n).is_searching_for_exit(),'Guide did not replace unknown search with remembered exit'
   report['runs'][-1]['guide_fixture']='Explicit co-located non-colliding stationary speaker; tests message transfer, not autonomous help selection.'
   event=sample('peer_guidance_received');assert 'MainEntrance:Peer' in event['npcs'][0]['knowledge'],'Guide source was not recorded'
   stage='guided';mark=now
  elif stage=='guided' and sim.get_elapsed_time()>=32:
   row=sample('guided_end');assert row['npcs'][0]['known']==1 and not row['npcs'][0]['searching']
   assert row['evacuated']>=1 and row['npcs'][0]['hidden'],'Guided visitor failed to physically evacuate'
   save();stop_run()
  elif stage=='look_away' and now-mark>=1:
   assert mem(npcs[0]).get_known_exit_count()==0,'An unseen exit was discovered behind the observer'
   sample('still_looking_away');npcs[0].set_actor_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0),False);stage='look_at_exit';mark=now
  elif stage=='look_at_exit' and now-mark>=1:
   row=sample('exit_seen_before_fire');assert row['npcs'][0]['known']==1 and 'MainEntrance:Seen' in row['npcs'][0]['knowledge'],'Looking at clear exit did not teach it'
   sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('alarm_trigger_offset_seconds',0);sim.start_simulation();stage='seen_escape';mark=now
  elif stage=='seen_escape' and now-mark>=25:
   row=sample('seen_exit_escape');assert row['evacuated']==1,'Visible learned exit did not permit normal evacuation'
   report['complete']=True;save();stop_run()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;stopping=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
