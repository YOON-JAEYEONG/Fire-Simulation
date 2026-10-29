import unreal,json,time,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered Main PIE, temporary editor-only exit markers, runtime palette NPCs. Opposing streams / stationary peer then natural release / actual interaction door. Fixed BIN1 frame0 isolates traffic; cognition early alarm and sight range1 fixture only. No saved assets.','runs':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main')
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for label,x in [('TrafficWest',100),('TrafficEast',2900)]:
 e=ea.spawn_actor_from_class(unreal.YUFSExitPoint,unreal.Vector(x,-900,5));e.set_editor_property('exit_id',label);e.set_actor_label(label)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
case=0;stage='setup';stopping=False;ending=False;restart=None;began=time.monotonic();last=-1;npcs=[];operator=None;door=None

def vec(v):return [v.x,v.y,v.z]
def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def spawn(w,cls,x,target):
 p=unreal.NavigationSystemV1.project_point_to_navigation(w,unreal.Vector(x,-900,5),None,None,unreal.Vector(30,30,35));assert p is not None
 tr=unreal.Transform(location=p+unreal.Vector(0,0,90))
 api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));assert n is not None,'Spawn blocked at '+str(x)
 n.set_editor_property('enable_everyday_behavior',False)
 m=n.get_component_by_class(unreal.YUFSExitKnowledgeComponent);m.set_editor_property('remember_marked_entry',False);m.set_editor_property('InitiallyKnownExitIDs',[target])
 cfg=unreal.YUFSBehaviorConfig();cfg.set_editor_property('early_alarm_response_fraction',1);cfg.set_editor_property('verify_alarm_response_fraction',0);cfg.set_editor_property('preparation_duration',0)
 n.get_component_by_class(unreal.YUFSBehaviorStateMachine).set_editor_property('config',cfg)
 sense=unreal.YUFSPerceptionConfig();sense.set_editor_property('vision_range',1)
 n.get_component_by_class(unreal.YUFSNPCPerceptionComponent).set_editor_property('config',sense)
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return n

def sample(label):
 row={'label':label,'time':sim.get_elapsed_time(),'frame':volume.get_frame(),'evacuated':sim.get_evacuated_count(),'npcs':[]}
 for i,n in enumerate(npcs):
  row['npcs'].append({'i':i,'name':n.get_name(),'xyz':vec(n.get_actor_location()),'hidden':n.get_editor_property('hidden'),'goal':vec(n.get_component_by_class(unreal.YUFSSmokeAwareNavigator).get_requested_destination()),'velocity':vec(n.get_velocity()),'traffic':n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_traffic_diagnostics(),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name(),'audit':n.get_hazard_audit()})
 if operator:row['operator']={'xyz':vec(operator.get_actor_location()),'hidden':operator.get_editor_property('hidden'),'velocity':vec(operator.get_velocity()),'audit':operator.get_hazard_audit(),'nav':str(operator.get_component_by_class(unreal.YUFSSmokeAwareNavigator).get_navigation_status()),'traffic':operator.get_component_by_class(unreal.YUFSLocalMovementComponent).get_traffic_diagnostics()}
 if door:row['door']={'fraction':door.get_open_fraction(),'blocked':door.is_opening_blocked(),'passage_clear':door.is_passage_clear()}
 report['runs'][-1]['samples'].append(row);save()

def tick(dt):
 global case,stage,stopping,ending,restart,last,npcs,sim,volume,operator,door,aligned,mark
 try:
  now=time.monotonic()
  if stopping:
   if level.is_in_play_in_editor():return
   if ending:
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
   if restart is None:restart=now;return
   if now-restart<1:return
   case+=1;stage='setup';stopping=False;npcs=[];operator=None;door=None;last=-1;level.editor_request_begin_play();return
  if now-began>240:raise RuntimeError('Full traffic probe timeout')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget));cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
   if case==0:
    for x in [650,750,850,950,1050,1150]:npcs.append(spawn(w,cls,x,'TrafficEast'))
    for x in [1850,1950,2050,2150,2250,2350]:npcs.append(spawn(w,cls,x,'TrafficWest'))
   else:
    for x in [650,750,850,950,1050,1150]:npcs.append(spawn(w,cls,x,'TrafficEast'))
    if case==1:
     operator=spawn(w,cls,1500,'TrafficEast');operator.set_editor_property('use_external_navigation_driver',True);operator.character_movement.set_component_tick_enabled(False)
    if case==2:
     tr=unreal.Transform(location=unreal.Vector(1500,-950,5));api=unreal.get_default_object(unreal.GameplayStatics)
     door=api.call_method('BeginDeferredActorSpawnFromClass',(w,unreal.YUFSInteractionDoor,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
     door.set_editor_property('open_seconds',4.0);api.call_method('FinishSpawningActor',(door,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
   report['runs'].append({'case':case,'initial':[vec(n.get_actor_location()) for n in npcs],'samples':[]})
   sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('alarm_trigger_offset_seconds',0);sim.start_simulation();stage='align'
  elif stage=='align' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   volume.set_frame(0);stage='run';mark=now;sample('aligned')
  elif stage=='run':
   elapsed=sim.get_elapsed_time()
   if int(elapsed)!=last:
    last=int(elapsed);sample('progress')
   if operator and elapsed>=25 and not report['runs'][-1].get('released'):
    sample('stationary_before_release');operator.set_editor_property('use_external_navigation_driver',False);operator.character_movement.set_component_tick_enabled(True);report['runs'][-1]['released']=True;save()
   if elapsed>=45:
    sample('end');stopping=True;ending=case==2
    if ending:report['complete']=True;save()
    level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;stopping=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
