import unreal, json, time, traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered saved Main PIE. Two stationary palette-class NPCs, actual BIN1 fixed frame480; exposure scales3/1. No forced behavior state, no saved assets. Validate visible incapacitation, actual animation clock, pause, review and real Stop/reload.', 'samples':[], 'events':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
level.load_level('/Game/Maps/Main')
stage='setup'; started=time.monotonic(); mark=started; last=-1; ending=False; npcs=[]

def save(): out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def vec(v): return [v.x,v.y,v.z]
def spawn(w,cls,xyz):
 tr=unreal.Transform(location=unreal.Vector(*xyz))
 api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 return n
def sample(label):
 value={'label':label,'sim_time':sim.get_elapsed_time(),'phase':str(sim.get_current_phase()),'fire_frame':volume.get_frame(),'count':sim.get_incapacitated_count(),'evacuated':sim.get_evacuated_count(),'npcs':[]}
 for n in npcs:
  value['npcs'].append({'name':n.get_name(),'hidden':n.get_editor_property('hidden'),'xyz':vec(n.get_actor_location()),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name(),'cursor':n.mesh.get_position(),'mesh_tick':n.mesh.is_component_tick_enabled(),'mesh_paused':n.mesh.get_editor_property('pause_anims'),'actor_tick':n.is_actor_tick_enabled(),'nav_tick':n.get_component_by_class(unreal.YUFSSmokeAwareNavigator).is_component_tick_enabled(),'movement_tick':n.character_movement.is_component_tick_enabled(),'movement_mode':str(n.character_movement.movement_mode),'collision':n.get_actor_enable_collision(),'audit':n.get_hazard_audit()})
 report['samples'].append(value);save();return value
def expected_pose(label,target):
 sim.seek_timeline_by_seconds(target)
 frame=sim.get_component_by_class(unreal.YUFSTimelineRecorder).get_current_review_frame_snapshot()
 snap=next(s for s in frame.get_editor_property('npc_snapshots') if str(s.get_editor_property('npc_id'))==npcs[0].get_name())
 actual=sample(label)['npcs'][0]
 assert actual['hidden']==(not snap.get_editor_property('visible')), 'Recorded visibility mismatch'
 assert abs(actual['cursor']-snap.get_editor_property('animation_position'))<.002, 'Recorded clip cursor mismatch'
 assert actual['animation']==snap.get_editor_property('animation_clip').get_name(), 'Recorded animation asset mismatch'
 assert actual['collision']==(snap.get_editor_property('visible') and not snap.get_editor_property('incapacitated') and not snap.get_editor_property('evacuated')), 'Replay terminal collision differs from live policy'
 report['events'].append({'label':label,'selected_time':frame.get_editor_property('fire_elapsed_time'),'recorded_count':frame.get_editor_property('incapacitated_count'),'incapacitated':snap.get_editor_property('incapacitated'),'visible':snap.get_editor_property('visible'),'cursor':actual['cursor']});save()
 return frame,snap

def tick(dt):
 global stage,mark,last,ending,sim,volume,cls,pause_sample,terminal,terminal_time,held
 try:
  if ending:
   if not level.is_in_play_in_editor():
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
   return
  now=time.monotonic()
  if now-started>150: raise RuntimeError('Probe exceeded150 wall seconds')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController)
   volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget))
   cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
   report['collapse_length']=unreal.load_asset('/Game/NPCs/Dying').get_play_length()
   for scale,xyz in [(3,(1200,-300,440)),(1,(1200,-450,440))]:
    n=spawn(w,cls,xyz);n.set_editor_property('enable_everyday_behavior',False);n.set_editor_property('use_external_navigation_driver',True)
    n.character_movement.set_movement_mode(unreal.MovementMode.MOVE_WALKING);n.character_movement.set_component_tick_enabled(False)
    cfg=unreal.YUFSBehaviorConfig();cfg.set_editor_property('smoke_exposure_time_scale',scale)
    n.get_component_by_class(unreal.YUFSBehaviorStateMachine).set_editor_property('config',cfg);npcs.append(n)
   sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('timeline_record_end_fire_seconds',54)
   sim.start_simulation();stage='align'
  elif stage=='align' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   volume.set_frame(480);sample('fixed480_start');stage='exposure'
  elif stage in ['exposure','collapse','held']:
   assert volume.get_frame()==480,'Controlled BIN frame drifted'
   t=int(sim.get_elapsed_time())
   if t!=last:last=t;sample(stage)
   if stage=='exposure' and sim.get_incapacitated_count()==1:
    terminal=sample('first_incapacitated');terminal_time=sim.get_elapsed_time()
    n=terminal['npcs'][0]
    assert not n['hidden'] and n['animation']=='Dying','Incapacitated native body did not remain visible'
    assert not n['actor_tick'] and not n['nav_tick'] and not n['movement_tick'],'Terminal gameplay clocks still running'
    assert n['mesh_tick'] and not n['collision'],'Presentation/collision policy mismatch'
    sim.pause_simulation();sim.pause_simulation();pause_sample=sample('terminal_pause_start');stage='paused';mark=now
   elif stage=='collapse' and sim.get_elapsed_time()>=terminal_time+report['collapse_length']+1:
    held=sample('collapse_end');assert held['npcs'][0]['cursor']>terminal['npcs'][0]['cursor']+.1,'Collapse animation never advanced'
    assert held['npcs'][0]['xyz']==terminal['npcs'][0]['xyz'],'Terminal body moved'
    stage='held';mark=now
   elif stage=='held' and now-mark>=1:
    end=sample('final_pose_held');assert abs(end['npcs'][0]['cursor']-held['npcs'][0]['cursor'])<.001,'Final pose looped or drifted'
    assert end['count']==1 and end['evacuated']==0,'Count duplicated or terminal became escaped'
    stage='review'
  elif stage=='paused' and now-mark>=2:
   end=sample('terminal_pause_after2s')
   for key in ['sim_time','count','fire_frame']:assert end[key]==pause_sample[key], 'Pause advanced '+key
   for key in ['cursor','xyz']:assert end['npcs'][0][key]==pause_sample['npcs'][0][key],'Pause advanced terminal '+key
   assert end['npcs'][0]['mesh_paused'],'Terminal mesh was not paused'
   sim.resume_simulation();volume.set_frame(480);stage='collapse'
  elif stage=='review' and sim.get_current_phase()==unreal.SimPhase.TIMELINE_REVIEW:
   expected_pose('seek_before_collapse',30)
   f,s=expected_pose('seek_collapse',terminal_time+.5)
   assert f.get_editor_property('incapacitated_count')==1 and s.get_editor_property('incapacitated') and s.get_editor_property('visible')
   sim.pause_timeline();held=sample('review_terminal_hold');stage='review_hold';mark=now
  elif stage=='review_hold' and now-mark>=2:
   end=sample('review_terminal_after2s');assert end['npcs'][0]['cursor']==held['npcs'][0]['cursor'],'Review cursor ran independently'
   expected_pose('seek_start',0)
   assert npcs[0].capsule_component.get_scaled_capsule_half_height()==88,'Rewind did not restore standing collision'
   expected_pose('seek_terminal_end',53)
   assert sim.get_incapacitated_count()==1,'Seeking changed live result count'
   sim.stop_and_reset_simulation();stage='reload'
  elif stage=='reload':
   fresh_sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController)
   if not fresh_sim or fresh_sim==sim:return
   sim=fresh_sim;volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   assert sim.get_current_phase()==unreal.SimPhase.WAITING_TO_START and sim.get_incapacitated_count()==0,'Reset retained terminal result'
   npcs.clear();n=spawn(w,cls,(1200,-300,440));npcs.append(n)
   fresh=sample('fresh_after_reset')['npcs'][0]
   assert not fresh['hidden'] and fresh['actor_tick'] and fresh['movement_tick'] and fresh['collision'],'Fresh actor inherited resolved flags'
   assert 'exposure=0.0000' in fresh['audit'],'Reset inherited dose'
   report['complete']=True;save();ending=True;level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
