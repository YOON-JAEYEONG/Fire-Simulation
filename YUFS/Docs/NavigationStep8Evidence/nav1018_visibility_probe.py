import unreal,json,time,traceback,re
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Rendered saved Main PIE.3 actual datasets, stationary identical-position palette NPC pairs compare attenuation0 vs1.2 at fixed480, eight headings each; then4 native movers with progressing fire1. Runtime fixtures only, no saved assets.','runs':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
case=0;stage='setup';stopping=False;ending=False;restart=None;began=time.monotonic();mark=began;heading=0;last=-1;npcs=[]

def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def vec(v):return [v.x,v.y,v.z]
def spawn(w,cls,xyz):
 tr=unreal.Transform(location=unreal.Vector(*xyz));api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return n
def sample(label):
 rows=[]
 for n in npcs:
  sense=n.get_component_by_class(unreal.YUFSNPCPerceptionComponent)
  audit=n.get_hazard_audit()
  rows.append({'name':n.get_name(),'xyz':vec(n.get_actor_location()),'yaw':n.get_actor_rotation().yaw,'breathing_height':n.get_breathing_height_cm(),'sight_cm':sense.get_center_visible_distance_cm(),'transmission':sense.get_center_sight_transmission(),'coverage':sense.has_complete_center_sight_data(),'self_smoke':float(re.search(r' self=([-0-9.]+)',audit)[1]),'audit':audit})
 row={'label':label,'case':case,'sim_time':sim.get_elapsed_time(),'frame':volume.get_frame(),'evacuated':sim.get_evacuated_count(),'incapacitated':sim.get_incapacitated_count(),'npcs':rows}
 report['runs'][-1]['samples'].append(row);save();return row
def stop_run():
 global stopping,restart,ending
 stopping=True;restart=None;ending=case==3;level.editor_request_end_play()

def tick(dt):
 global case,stage,stopping,ending,restart,mark,heading,last,npcs,sim,volume,initial
 try:
  now=time.monotonic()
  if stopping:
   if level.is_in_play_in_editor():return
   if ending:
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
   if restart is None:restart=now;return
   if now-restart<1:return
   case+=1;stage='setup';stopping=False;npcs=[];heading=0;last=-1;level.editor_request_begin_play();return
  if now-began>150:raise RuntimeError('Probe exceeded150 wall seconds')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget))
   cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
   sim.select_fire_option(case if case<3 else 0)
   placements=[(1200,-300,440)]*2 if case<3 else [(1100,-300,350),(1400,-450,350),(1700,-650,350),(2000,-500,350)]
   for i,xyz in enumerate(placements):
    if case==3:
     floor=unreal.NavigationSystemV1.project_point_to_navigation(w,unreal.Vector(*xyz),None,None,unreal.Vector(45,45,45))
     assert floor is not None,'Normal-run spawn lacks actual navigation support'
     xyz=vec(floor+unreal.Vector(0,0,90))
    n=spawn(w,cls,xyz);n.set_editor_property('enable_everyday_behavior',False)
    if case<3:
     n.set_editor_property('use_external_navigation_driver',True);n.character_movement.set_component_tick_enabled(False)
     cfg=unreal.YUFSPerceptionConfig();cfg.set_editor_property('smoke_sight_attenuation_per_meter',0 if i==0 else 1.2)
     n.get_component_by_class(unreal.YUFSNPCPerceptionComponent).set_editor_property('config',cfg)
     mind_cfg=unreal.YUFSBehaviorConfig();mind_cfg.set_editor_property('smoke_exposure_time_scale',1)
     n.get_component_by_class(unreal.YUFSBehaviorStateMachine).set_editor_property('config',mind_cfg)
    npcs.append(n)
   initial=[vec(n.get_actor_location()) for n in npcs]
   report['runs'].append({'case':case,'initial':initial,'samples':[]})
   sim.set_editor_property('fire_start_delay_seconds',0);sim.start_simulation();stage='align';mark=now
  elif stage=='align' and sim.get_current_phase()==unreal.SimPhase.FIRE_ACTIVE:
   if case<3:volume.set_frame(480)
   stage='observe';mark=now
  elif stage=='observe':
   if case<3:
    assert volume.get_frame()==480,'Fixed-frame fixture drifted'
    if now-mark<.9:return
    row=sample('heading_'+str(heading*45))
    assert all('perceptionFrame=480' in n['audit'] and 'eyeStatus=Ready' in n['audit'] for n in row['npcs']),'Pair did not receive the same real frame'
    assert abs(row['npcs'][0]['self_smoke']-row['npcs'][1]['self_smoke'])<.0001,'Optical setting altered physical smoke exposure'
    assert row['npcs'][1]['sight_cm']<=row['npcs'][0]['sight_cm']+.1,'Attenuation lengthened the same geometric sight line'
    assert row['npcs'][0]['xyz']==row['npcs'][1]['xyz'],'Pair positions diverged'
    if heading==7:
     reductions=[s['npcs'][0]['sight_cm']-s['npcs'][1]['sight_cm'] for s in report['runs'][-1]['samples']]
     report['runs'][-1]['max_reduction_cm']=max(reductions)
     if case==2:assert any(r.get('max_reduction_cm',0)>10 for r in report['runs']),'No opacity effect found in any actual smoke heading'
     save();stop_run()
    else:
     heading+=1
     for n in npcs:n.set_actor_rotation(unreal.Rotator(pitch=0,yaw=heading*45,roll=0),False)
     mark=now
   else:
    elapsed=int(sim.get_elapsed_time())
    if elapsed!=last:
     last=elapsed;row=sample('progressing_fire')
     if elapsed>=25:
      distances=[sum((a-b)**2 for a,b in zip(n['xyz'],p))**.5 for n,p in zip(row['npcs'],initial)]
      report['runs'][-1]['displacement_cm']=distances
      assert any(d>150 for d in distances),'All normal NPCs remained stationary'
      assert row['frame']>100,'Normal fire did not advance'
      report['complete']=True;save();stop_run()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;stopping=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
