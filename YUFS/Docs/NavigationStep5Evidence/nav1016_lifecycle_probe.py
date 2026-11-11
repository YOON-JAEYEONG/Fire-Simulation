import unreal,json,time,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered saved Main PIE, runtime palette NPCs and one temporary door. Explicit authored suppression gesture for lifecycle coverage only; no claims of autonomous suppression or fire mutation. Pause/resume, recorded native pose/props, seek/play/pause, then actual Stop/reload and a second Start. No assets saved.','events':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
level.load_level('/Game/Maps/Main')
stage='setup';mark=time.monotonic();started=mark;ending=False

def save(): out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def spawn(w,cls,xyz):
 tr=unreal.Transform(location=unreal.Vector(*xyz))
 n=unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 return n

def vec(p): return [p.x,p.y,p.z]
def sample(label):
 nav=n.get_component_by_class(unreal.YUFSSmokeAwareNavigator)
 state={'label':label,'sim_time':sim.get_elapsed_time(),'phase':str(sim.get_current_phase()),'frame':volume.get_frame(),'npc_xyz':vec(n.get_actor_location()),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name(),'clip_position':n.mesh.get_position(),'mesh_paused':n.mesh.get_editor_property('pause_anims'),'actor_tick':n.is_actor_tick_enabled(),'nav_tick':nav.is_component_tick_enabled(),'movement_tick':n.character_movement.is_component_tick_enabled(),'tool_state':str(tool.get_extinguisher_state()),'tool_owner':tool.get_owner_actor().get_name() if tool.get_owner_actor() else None,'tool_fuel':tool.get_remaining_agent_normalized(),'tool_xyz':vec(tool.get_actor_location()),'door_fraction':door.get_open_fraction(),'exposure':n.get_hazard_audit()}
 report['events'].append(state);save();return state

def tick(dt):
 global stage,mark,ending,sim,volume,bm,n,user,tool,door,w0,pause_sample,review_sample,frames,cls
 try:
  if ending:
   if not level.is_in_play_in_editor():
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
   return
  now=time.monotonic()
  if now-started>90: raise RuntimeError('Lifecycle probe exceeded 90 seconds')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   w0=w;sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController)
   volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget))
   cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
   n=spawn(w,cls,(3500,-300,90));n.set_editor_property('enable_everyday_behavior',False)
   user=spawn(w,cls,(5140,-450,90));user.set_editor_property('enable_everyday_behavior',False);user.set_editor_property('use_external_navigation_driver',True);user.character_movement.set_component_tick_enabled(False)
   tool=spawn(w,unreal.YUFSFireExtinguisher,(3560,-300,3))
   door=spawn(w,unreal.YUFSInteractionDoor,(5000,-500,0));door.set_editor_property('open_seconds',6)
   sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('timeline_record_end_fire_seconds',12)
   sim.start_simulation();stage='live';mark=now
  elif stage=='live' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE and sim.get_elapsed_time()>.5:
   assert n.get_component_by_class(unreal.YUFSNpcSuppressionComponent).start_visual_presentation(tool,unreal.Vector(3900,-300,65),8),'Explicit presentation failed'
   assert door.try_use(user),'Door fixture cannot start'
   stage='to_pause'
  elif stage=='to_pause' and sim.get_elapsed_time()>=3:
   assert tool.get_extinguisher_state()==unreal.YUFSFireExtinguisherState.SPRAYING,'Fixture was not spraying before pause'
   sim.pause_simulation();sim.pause_simulation();pause_sample=sample('paused_start');stage='paused';mark=now
  elif stage=='paused' and now-mark>=2:
   end=sample('paused_after_2_wall_seconds')
   for key in ['sim_time','frame','npc_xyz','clip_position','tool_state','tool_owner','tool_fuel','tool_xyz','door_fraction']:
    assert end[key]==pause_sample[key],f'Pause advanced {key}'
   assert not end['actor_tick'] and not end['nav_tick'] and not end['movement_tick'] and end['mesh_paused']
   sim.resume_simulation();stage='to_review';mark=now
  elif stage=='to_review' and sim.get_current_phase()==unreal.SimPhase.TIMELINE_REVIEW:
   sample('automatic_review_end')
   recorder=sim.get_component_by_class(unreal.YUFSTimelineRecorder)
   assert recorder.get_recorded_frame_count()>20,'Too few recorded frames'
   # Inspect the exact stored frame chosen by the same nearest-time rule.
   sim.seek_timeline_by_seconds(5)
   expected=recorder.get_current_review_frame_snapshot()
   ns=next(s for s in expected.get_editor_property('npc_snapshots') if str(s.get_editor_property('npc_id'))==n.get_name())
   ts=next(s for s in expected.get_editor_property('ExtinguisherSnapshots') if str(s.get_editor_property('ToolId'))==tool.get_name())
   ds=next(s for s in expected.get_editor_property('DoorSnapshots') if str(s.get_editor_property('DoorId'))==door.get_name())
   restored=sample('seek_5_exact_recorded_pose')
   assert abs(restored['clip_position']-ns.get_editor_property('AnimationPosition'))<.002,'Native clip cursor mismatch'
   assert restored['animation']==ns.get_editor_property('AnimationClip').get_name(),'Native clip mismatch'
   assert abs(restored['door_fraction']-ds.get_editor_property('OpenFraction'))<.0001,'Door fraction mismatch'
   assert tool.get_extinguisher_state()==ts.get_editor_property('State'),'Tool state mismatch'
   assert volume.get_frame()==expected.get_editor_property('fire_frame'),'Fire review frame mismatch'
   assert not tool.get_owner_actor() and not tool.try_reserve(n) and not door.try_use(user),'Review acquired gameplay ownership'
   sim.pause_timeline();review_sample=sample('review_pause_start');stage='review_pause';mark=now
  elif stage=='review_pause' and now-mark>=2:
   end=sample('review_pause_after_2_wall_seconds')
   for key in ['frame','npc_xyz','clip_position','tool_state','tool_fuel','tool_xyz','door_fraction']:
    assert end[key]==review_sample[key],f'Review pause advanced {key}'
   sim.seek_timeline_by_seconds(0);sample('seek_start');sim.play_timeline();stage='review_play';mark=now
  elif stage=='review_play' and now-mark>=2:
   sim.pause_timeline();sample('review_play_then_pause');assert sim.get_timeline_current_time()>1,'Review cursor did not advance'
   sim.stop_and_reset_simulation();stage='reload';mark=now
  elif stage=='reload':
   new_sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController)
   if not new_sim or new_sim==sim:return
   sim=new_sim;volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   assert sim.get_current_phase()==unreal.SimPhase.WAITING_TO_START,'Reload did not reset phase'
   assert sim.get_elapsed_time()==0 and sim.is_npc_activity_enabled(),'Reload leaked clock/pause state'
   fresh=spawn(w,cls,(3500,-300,90));fresh.set_editor_property('enable_everyday_behavior',False)
   assert fresh.mesh.get_editor_property('pause_anims')==False,'New NPC inherited animation pause'
   assert 'exposure=0.0000' in fresh.get_hazard_audit(),'New NPC inherited exposure'
   assert fresh.capsule_component.get_scaled_capsule_half_height()==88,'New NPC inherited low capsule'
   report['restart']={'phase':str(sim.get_current_phase()),'clock':sim.get_elapsed_time(),'npc':fresh.get_hazard_audit(),'mesh_paused':fresh.mesh.get_editor_property('pause_anims'),'tools_available':all(not t.get_owner_actor() for t in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.YUFSFireExtinguisher))};save()
   sim.set_editor_property('fire_start_delay_seconds',0);sim.start_simulation();stage='restarted';mark=now
  elif stage=='restarted' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE and now-mark>=1:
   report['second_start']={'phase':str(sim.get_current_phase()),'clock':sim.get_elapsed_time(),'frame':volume.get_frame()};report['complete']=True;save();ending=True;level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()