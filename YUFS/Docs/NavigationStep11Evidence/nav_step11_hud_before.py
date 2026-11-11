import unreal,json,time,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered saved Main. Actual palette NPCs: one mobile exit user(scale1), one stationary smoke-dose NPC(scale3), one late stationary control(scale1). Runtime fire delay2/record46, real BIN1 fixed480. No forced terminal states, saved assets or BP edits. HUD bindings/actual Slate text, seek/pause/play and real Stop/reload/new record.','samples':[],'bindings':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
stage='setup';began=time.monotonic();mark=began;last=-1;ending=False;late=False;npcs=[];seek_index=0

def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def field(o,k):return o.get_editor_property(k)
def vec(v):return [v.x,v.y,v.z]
def texts(h):
 result={}
 def walk(w):
  if isinstance(w,unreal.TextBlock):result[w.get_name()]=str(w.get_text())
  if isinstance(w,unreal.PanelWidget):
   for i in range(w.get_children_count()):walk(w.get_child_at(i))
 tree=field(h,'WidgetTree');walk(field(tree,'RootWidget'));return result

def spawn(w,xyz,scale,held=False):
 tr=unreal.Transform(location=unreal.Vector(*xyz));api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 cfg=unreal.YUFSBehaviorConfig();cfg.set_editor_property('smoke_exposure_time_scale',scale);cfg.set_editor_property('early_alarm_response_fraction',1);cfg.set_editor_property('verify_alarm_response_fraction',0);cfg.set_editor_property('preparation_duration',0)
 n.get_component_by_class(unreal.YUFSBehaviorStateMachine).set_editor_property('config',cfg);n.set_editor_property('enable_everyday_behavior',False);n.set_editor_property('use_external_navigation_driver',held)
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 if held:n.character_movement.set_component_tick_enabled(False)
 return n

def final_results():return [[field(r,'EvacuatedCount'),field(r,'IncapacitatedCount'),field(r,'TotalNPCCount'),field(r,'SimDurationSeconds')] for r in sim.get_all_run_results()]
def sample(label):
 row={'label':label,'phase':str(sim.get_current_phase()),'live_clock':sim.get_elapsed_time(),'live_evacuated':sim.get_evacuated_count(),'live_incap':sim.get_incapacitated_count(),'live_total':sim.get_total_npc_count(),'hud_status':str(hud.get_npc_status_text()),'hud_clock':str(hud.get_elapsed_time_text()),'hud_rate':hud.get_evacuation_rate_percent(),'slate_texts':texts(hud),'review_time':sim.get_timeline_current_time(),'volume_frame':volume.get_frame(),'npcs':[{'xyz':vec(a.get_actor_location()),'hidden':field(a,'hidden'),'audit':a.get_hazard_audit()} for a in npcs]}
 if sim.is_timeline_review_mode():
  f=sim.get_component_by_class(unreal.YUFSTimelineRecorder).get_current_review_frame_snapshot()
  row['recorded']={'fire_time':field(f,'FireElapsedTime'),'fire_frame':field(f,'FireFrame'),'evacuated':field(f,'EvacuatedCount'),'incap':field(f,'IncapacitatedCount'),'snapshots':len(field(f,'NPCSnapshots'))}
  if hasattr(sim,'get_display_stats'):
   for target,name in [('total','TotalNPCCount'),('sim_time','SimulationElapsedTime')]:row['recorded'][target]=field(f,name)
 if hasattr(sim,'get_display_stats'):
  v=sim.get_display_stats();row['display']={k:field(v,k) for k in ['ElapsedSimTime','EvacuatedCount','IncapacitatedCount','TotalNPCCount','bHasData']}
 report['samples'].append(row);save();return row

def verify(row):
 if 'display' not in row or 'recorded' not in row:return
 f=row['recorded'];v=row['display']
 assert [v['EvacuatedCount'],v['IncapacitatedCount'],v['TotalNPCCount']]==[f['evacuated'],f['incap'],f['total']],'Displayed counts differ from applied pose frame'
 assert abs(v['ElapsedSimTime']-f['sim_time'])<.001
 assert abs(row['hud_rate']-(100*f['evacuated']/f['total'] if f['total'] else 0))<.001
 assert row['hud_status'] in row['slate_texts'].values(),'Visible Slate status binding differs from HUD getter'
 assert row['hud_clock'] in row['slate_texts'].values(),'Visible Slate clock binding differs from HUD getter'
 assert final_results()==stored,'Seeking mutated final run results'

def tick(dt):
 global stage,mark,last,ending,late,npcs,sim,volume,hud,cls,stored,targets,seek_index,paused,old_world
 try:
  now=time.monotonic()
  if ending:
   if not level.is_in_play_in_editor():
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
   return
  if now-began>150:raise RuntimeError('HUD probe timeout')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   widgets=unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False);hud=next(a for a in widgets if isinstance(a,unreal.YUFSSimHUD))
   palette=next(a for a in widgets if isinstance(a,unreal.YUFSNPCPaletteWidget));cls=field(field(palette,'available_np_cs')[0],'npc_class')
   report['hud_class']=hud.get_class().get_path_name();report['new_api']=hasattr(sim,'get_display_stats')
   npcs=[spawn(w,(2200,-350,90),1),spawn(w,(1200,-300,440),3,True)]
   sim.set_editor_property('fire_start_delay_seconds',2);sim.set_editor_property('alarm_trigger_offset_seconds',0);sim.set_editor_property('timeline_record_end_fire_seconds',46)
   sample('before_start');hud.on_start_button_clicked();stage='align'
  elif stage=='align' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   volume.set_frame(480);stage='run';sample('aligned480')
  elif stage=='run':
   if int(sim.get_elapsed_time())!=last:last=int(sim.get_elapsed_time());sample('live')
   if sim.get_elapsed_time()>=12 and not late:
    npcs.append(spawn(w,(1200,-450,440),1,True));late=True;sample('late_npc_added')
   if sim.is_timeline_review_mode():
    assert sim.get_evacuated_count()>=1 and sim.get_incapacitated_count()>=1,'Fixture did not produce both exit and actual dose incap'
    stored=final_results();report['stored_result']=stored;targets=[0,5,15,30,40,sim.get_timeline_max_time()];seek_index=0;stage='seek';mark=now
    hud.on_timeline_pause_button_clicked();hud.on_timeline_slider_changed(0)
  elif stage=='seek' and now-mark>=.15:
   row=sample('seek_'+str(targets[seek_index]));verify(row);seek_index+=1
   if seek_index<len(targets):hud.on_timeline_slider_changed(targets[seek_index]/sim.get_timeline_max_time());mark=now
   else:
    hud.on_timeline_slider_changed(0);stage='hold_begin';mark=now
  elif stage=='hold_begin' and now-mark>=.15:
   paused=sample('review_pause_start');verify(paused);stage='hold';mark=now
  elif stage=='hold' and now-mark>=2:
   row=sample('review_pause_after2s');verify(row)
   for k in ['hud_status','hud_clock','hud_rate','review_time','volume_frame']:assert row[k]==paused[k],'Review hold drifted '+k
   hud.on_timeline_play_button_clicked();stage='play';mark=now
  elif stage=='play' and now-mark>=1.2:
   hud.on_timeline_pause_button_clicked();row=sample('review_played');verify(row);assert row['review_time']>paused['review_time']+.5
   old_world=w;hud.on_stop_button_clicked();npcs=[];stage='reload'
  elif stage=='reload' and w!=old_world:
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   if not sim:return
   hud=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSSimHUD))
   row=sample('reset_waiting');assert row['live_evacuated']==0 and row['live_incap']==0 and row['live_total']==0
   npcs=[spawn(w,(1200,-300,440),1,True)];sim.set_editor_property('fire_start_delay_seconds',0);sim.set_editor_property('timeline_record_end_fire_seconds',.5);hud.on_start_button_clicked();stage='fresh_review'
  elif stage=='fresh_review' and sim.is_timeline_review_mode():
   stored=final_results();row=sample('fresh_recording');verify(row);assert row['live_total']==1 and row['live_evacuated']==0 and row['live_incap']==0
   report['complete']=True;save();ending=True;level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
