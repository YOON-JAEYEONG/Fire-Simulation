import unreal,json,time,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered Main native physical integration stress: locked door, hot door, natural rule-fallback help and door, authored spray gesture and door. Six identical early-alarm followers, temporary exit target and props, fixed BIN1 frame0 isolate local interaction traffic. Help case explicitly selects existing rule fallback, no forced help result. Spray case is authored visual presentation, NOT an FDS suppression or fire-extinguishing test. No saved map edits.','runs':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main');ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
e=ea.spawn_actor_from_class(unreal.YUFSExitPoint,unreal.Vector(2900,-900,5));e.set_editor_property('exit_id','MixedEast');e.set_actor_label('MixedEast')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
case=0;stage='setup';stopping=False;ending=False;restart=None;began=time.monotonic();last=-1;npcs=[];operator=None;recipient=None;tool=None;door=None
def field(o,k):return o.get_editor_property(k)
def vec(v):return [v.x,v.y,v.z]
def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def spawn(w,xyz,i):
 tr=unreal.Transform(location=unreal.Vector(*xyz));api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));assert n
 n.set_editor_property('StableNPCId',2000+i);n.set_editor_property('enable_everyday_behavior',False)
 n.set_editor_property('data_collection_mode',case==2)
 m=n.get_component_by_class(unreal.YUFSExitKnowledgeComponent);m.set_editor_property('remember_marked_entry',False);m.set_editor_property('InitiallyKnownExitIDs',['MixedEast'])
 cfg=unreal.YUFSBehaviorConfig();cfg.set_editor_property('early_alarm_response_fraction',1);cfg.set_editor_property('verify_alarm_response_fraction',0);cfg.set_editor_property('preparation_duration',0)
 n.get_component_by_class(unreal.YUFSBehaviorStateMachine).set_editor_property('config',cfg)
 sense=unreal.YUFSPerceptionConfig();sense.set_editor_property('vision_range',1);n.get_component_by_class(unreal.YUFSNPCPerceptionComponent).set_editor_property('config',sense)
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return n
def spawn_actor(w,cls0,xyz,props):
 tr=unreal.Transform(location=unreal.Vector(*xyz));api=unreal.get_default_object(unreal.GameplayStatics)
 a=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls0,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 for k,v in props.items():a.set_editor_property(k,v)
 api.call_method('FinishSpawningActor',(a,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return a
def npc_row(n):
 env=n.get_component_by_class(unreal.YUFSNpcEnvironmentInteraction)
 return {'id':field(n,'StableNPCId'),'xyz':vec(n.get_actor_location()),'hidden':field(n,'hidden'),'collision':n.get_actor_enable_collision(),'velocity':vec(n.get_velocity()),'traffic':n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_traffic_diagnostics(),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name(),'receiving':env.is_receiving_assistance(),'contact':env.is_receiving_contact_assistance(),'needs':field(env,'needs_assistance'),'audit':n.get_hazard_audit()}
def sample(label):
 row={'label':label,'time':sim.get_elapsed_time(),'frame':volume.get_frame(),'evacuated':sim.get_evacuated_count(),'incap':sim.get_incapacitated_count(),'npcs':[npc_row(n) for n in npcs],'door':{'fraction':door.get_open_fraction(),'clear':door.is_passage_clear(),'operable':door.can_operate(),'locked':field(door,'locked'),'hot':field(door,'hot')}}
 if recipient:row['recipient']=npc_row(recipient)
 if operator:row['operator']=npc_row(operator)
 if tool:row['tool']={'state':str(tool.get_extinguisher_state()),'owner':str(tool.get_owner_actor()),'remaining':tool.get_remaining_agent_normalized(),'presentation':operator.get_component_by_class(unreal.YUFSNpcSuppressionComponent).is_visual_presentation_active()}
 report['runs'][-1]['samples'].append(row);save();return row
def tick(dt):
 global case,stage,stopping,ending,restart,last,npcs,sim,volume,cls,door,recipient,operator,tool
 try:
  now=time.monotonic()
  if stopping:
   if level.is_in_play_in_editor():return
   if ending:
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
   if restart is None:restart=now;return
   if now-restart<1:return
   case+=1;stage='setup';stopping=False;npcs=[];operator=recipient=tool=None;last=-1;level.editor_request_begin_play();return
  if now-began>420:raise RuntimeError('Mixed stress exceeded420s')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget));cls=field(field(palette,'available_np_cs')[0],'npc_class')
   for i,x in enumerate([650,750,850,950,1050,1150]):npcs.append(spawn(w,(x,-900,94.94),i))
   door=spawn_actor(w,unreal.YUFSInteractionDoor,(1850 if case>=2 else 1500,-950,5),{'open_seconds':4.0,'locked':case==0,'hot':case==1})
   if case==2:
    recipient=spawn(w,(1500,-900,94.94),10);recipient.get_component_by_class(unreal.YUFSNpcEnvironmentInteraction).request_assistance(True)
    recipient.set_editor_property('use_external_navigation_driver',True);recipient.character_movement.set_component_tick_enabled(False)
   if case==3:
    operator=spawn(w,(1500,-900,94.94),10)
    tool=spawn_actor(w,unreal.YUFSFireExtinguisher,(1500,-970,30),{})
   report['runs'].append({'case':case,'name':['locked','hot','help_and_door','authored_spray_and_door'][case],'initial':[vec(n.get_actor_location()) for n in npcs],'samples':[]})
   sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('alarm_trigger_offset_seconds',0);sim.start_simulation();stage='align'
  elif stage=='align' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   volume.set_frame(0)
   if case==3:assert operator.get_component_by_class(unreal.YUFSNpcSuppressionComponent).start_visual_presentation(tool,unreal.Vector(2300,-900,100),15),'Cannot start explicit authored spray fixture'
   stage='run';sample('aligned')
  elif stage=='run':
   t=sim.get_elapsed_time()
   if int(t)!=last:last=int(t);sample('progress')
   if recipient and not field(recipient.get_component_by_class(unreal.YUFSNpcEnvironmentInteraction),'needs_assistance') and not report['runs'][-1].get('help_completed'):
    sample('native_help_completed');report['runs'][-1]['help_completed']=True
    recipient.set_editor_property('use_external_navigation_driver',False);recipient.character_movement.set_component_tick_enabled(True)
   if t>=45:
    row=sample('end')
    if case<2:assert row['door']['fraction']==0 and not row['door']['clear'] and not row['door']['operable'],'Locked/hot door was incorrectly opened'
    if case==2:
     assert report['runs'][-1].get('help_completed'),'Native helping did not complete'
     assert any(r.get('recipient',{}).get('contact') for r in report['runs'][-1]['samples']),'Never observed real contact assistance'
    if case==3:
     assert any('SPRAYING' in r.get('tool',{}).get('state','').upper() for r in report['runs'][-1]['samples']),'Authored prop never sprayed'
     assert row['tool']['owner']=='None' and not row['tool']['presentation'],'Prop reservation/presentation leaked after authored completion'
     assert row['tool']['remaining']==1,'Authored presentation must not consume real agent'
    report['runs'][-1]['result']={'evacuated':row['evacuated'],'incap':row['incap'],'total':sim.get_total_npc_count(),'door_fraction':row['door']['fraction']}
    stopping=True;ending=case==3;restart=None
    if ending:report['complete']=True
    save();level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;stopping=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
