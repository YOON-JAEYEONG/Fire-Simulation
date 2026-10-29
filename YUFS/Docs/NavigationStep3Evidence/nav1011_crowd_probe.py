import unreal,json,time,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered Main PIE, frame0 paused to isolate traffic; 20 palette-class NPCs, transient alarm-responsive configs; no state/position overrides after spawn; ground doorway then upstairs stairs. No saved scene changes.','runs':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
case=0;prepared=False;aligned=False;stopping=False;ending=False;restart=None;started=time.monotonic();last=-1;npcs=[]
def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def tick(dt):
 global case,prepared,aligned,stopping,ending,restart,started,last,npcs,sim,volume,bm
 try:
  if stopping:
   if level.is_in_play_in_editor():return
   if ending:unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
   if restart is None:restart=time.monotonic();return
   if time.monotonic()-restart<1:return
   case+=1;prepared=False;aligned=False;stopping=False;restart=None;started=time.monotonic();last=-1;npcs=[];level.editor_request_begin_play();return
  w=editor.get_game_world()
  if not w:
   if time.monotonic()-started>120:raise RuntimeError('PIE timeout')
   return
  if not prepared:
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume);bm=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSBinaryManager)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget));cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class');half=unreal.get_default_object(cls).capsule_component.get_scaled_capsule_half_height()
   placements=[]
   for y in (-300,-410,-520,-630,-740,-850,-960):
    for x in (1100,1210,1320,1430,1540,1650,1760):
     floor=unreal.Vector(x,y,350 if case else 0)
     p=unreal.NavigationSystemV1.project_point_to_navigation(w,floor,None,None,unreal.Vector(35,35,40))
     if p is None or abs(p.z-floor.z)>35:continue
     if any((p-q).length()<95 for q in placements):continue
     tr=unreal.Transform(location=p+unreal.Vector(0,0,half+2))
     n=unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
     if not n:continue
     mind=n.get_component_by_class(unreal.YUFSBehaviorStateMachine)
     cfg=unreal.YUFSBehaviorConfig();cfg.set_editor_property('early_alarm_response_fraction',1.0);cfg.set_editor_property('verify_alarm_response_fraction',0.0);cfg.set_editor_property('preparation_duration',0.0)
     mind.set_editor_property('config',cfg)
     n.set_editor_property('enable_everyday_behavior',False)
     unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));npcs.append(n);placements.append(p)
     if len(npcs)>=20:break
    if len(npcs)>=20:break
   report['runs'].append({'case':case,'count':len(npcs),'positions':[str(n.get_actor_location()) for n in npcs],'samples':[]})
   if len(npcs)!=20:raise RuntimeError('Could not place 20 non-overlapping NPCs')
   sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('alarm_trigger_seconds',0) if False else None
   sim.start_simulation();prepared=True
  if not aligned:
   if sim.get_current_phase()!=unreal.SimPhase.FIRE_ACTIVE:return
   volume.set_frame(0);started=time.monotonic();aligned=True;return
  if volume.get_frame()!=0:raise RuntimeError('Traffic isolation frame changed')
  seconds=int(time.monotonic()-started)
  if seconds!=last:
   last=seconds
   report['runs'][-1]['samples'].append({'t':seconds,'phase':str(sim.get_current_phase()),'diagnostics':bm.get_hazard_diagnostics(),'npcs':[{'name':n.get_name(),'audit':n.get_hazard_audit(),'velocity':str(n.get_velocity()),'traffic':str(n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_state()),'yielding_to':n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_yielding_to(),'traffic_diagnostics':n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_traffic_diagnostics(),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name()} for n in npcs]});save()
   if seconds>=58:stopping=True;ending=case>=1;level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;stopping=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
