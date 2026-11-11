import unreal,json,time,traceback,struct,re
from pathlib import Path
out=Path(__file__).with_suffix('.json');r={'mode':'Real final-map PIE, actual option3 BIN frame40 fixed. Select a physically supported/navigable hot cell; hold one test NPC through external navigation ownership only. Native perception/traits/thresholds unchanged; scheduled comm off. No saved assets.','samples':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);ed=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/it관_찐최종/Main_level');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
stage='ready';ready=None;done=False;started=time.monotonic();last=-1
def f(o,k):return o.get_editor_property(k)
def save():out.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
def tick(dt):
 global stage,ready,done,last,sim,volume,n,marked
 try:
  if done:
   if not level.is_in_play_in_editor():
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
   return
  if time.monotonic()-started>100:raise RuntimeError('Heat test timeout')
  w=ed.get_game_world()
  if not w:return
  if ready is None:ready=time.monotonic();return
  if time.monotonic()-ready<1:return
  if stage=='ready':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   cls=f(f(unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.YUFSNPCPaletteWidget,False)[0],'available_np_cs')[0],'npc_class')
   file=Path(unreal.Paths.project_content_dir())/'Fires/FinalIT1104/BinaryData/smoke_data_3.bin'
   with file.open('rb') as h:
    nf,x,y,z=struct.unpack('<4i',h.read(16));count=x*y*z;h.seek(16+40*count*2+count);heat=h.read(count)
   candidates=sorted([(heat[(ix*y+iy)*z+5],ix,iy) for ix in range(x) for iy in range(y)],reverse=True)
   place=None
   for value,ix,iy in candidates:
    if value<190:break
    p=unreal.YUFSMapDiagnostics.probe_location(w,unreal.Vector(2200+(ix+.5)*50,-7000-(iy+.5)*50,20),cls)
    if f(p,'bSupported') and f(p,'bBodyClear') and f(p,'bOnNavigation'):
     place=f(p,'NavigationLocation');r['placement_heat_byte']=value;break
   assert place,'No physically valid hot test cell'
   tr=unreal.Transform(location=place+unreal.Vector(0,0,90));api=unreal.get_default_object(unreal.GameplayStatics)
   n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));assert n
   n.set_editor_property('data_collection_mode',True);n.set_editor_property('enable_everyday_behavior',False);n.set_editor_property('use_external_navigation_driver',True)
   api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
   sim.select_fire_option(2)
   for k in ('alarm_trigger_offset_seconds','pre_recorded_msg_offset_seconds','live_announcement_offset_seconds','staff_guidance_offset_seconds'):sim.set_editor_property(k,-1.)
   sim.set_editor_property('fire_start_delay_seconds',0.);sim.set_editor_property('timeline_record_end_fire_seconds',60.);sim.start_simulation();stage='start'
  elif stage=='start' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   volume.set_frame(40);marked=time.monotonic();stage='run'
  elif stage=='run':
   if int(time.monotonic()-marked)!=last:
    last=int(time.monotonic()-marked);sm=n.get_component_by_class(unreal.YUFSBehaviorStateMachine)
    r['samples'].append({'time':sim.get_elapsed_time(),'frame':volume.get_frame(),'audit':n.get_hazard_audit(),'cue':str(sm.get_decision_cue())});save()
   if time.monotonic()-marked>=8:
    assert any('HEAT' in s['cue'].upper() and 'state=Evacuating' in s['audit'] and 'alarm=0' in s['audit'] for s in r['samples']),str(r['samples'])
    r['complete']=True;save();done=True;level.editor_request_end_play()
 except Exception:r['error']=traceback.format_exc();save();done=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
