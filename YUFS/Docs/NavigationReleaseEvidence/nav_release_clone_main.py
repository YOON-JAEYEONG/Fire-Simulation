import unreal,json,time,traceback,math
from pathlib import Path
out=Path(__file__).with_suffix('.json')
report={'mode':'Actual saved Main, fresh-clone FireOption2, progressing recorded fire with no SetFrame. Twenty original NavMesh-validated starting coordinates read from the source-checkout test fixture, matching stable IDs, DONT_SPAWN_IF_COLLIDING. Only coordinates are reused; game assets/data/DLL come from fresh clone. Avoid Python static NavigationSystem CDO creation ensure. Default cognition/exposure/knowledge/timeline, runtime fireDelay0 and everyday disabled only. No saved asset edits or artificial exit markers. Single runs, not population statistics.','runs':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
case=1;stage='setup';stopping=False;ending=False;restart=None;began=time.monotonic();last=-1;npcs=[];positions=json.loads((Path(__file__).parent/'nav_release_normal.json').read_text(encoding='utf8'))['runs'][1]['initial']
def field(o,k):return o.get_editor_property(k)
def vec(v):return [v.x,v.y,v.z]
def save():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def sample(label):
 row={'label':label,'phase':str(sim.get_current_phase()),'time':sim.get_elapsed_time(),'frame':volume.get_frame(),'binary':binary.get_hazard_diagnostics(),'evacuated':sim.get_evacuated_count(),'incap':sim.get_incapacitated_count(),'total':sim.get_total_npc_count(),'npcs':[]}
 for n in npcs:
  row['npcs'].append({'id':field(n,'StableNPCId'),'xyz':vec(n.get_actor_location()),'hidden':field(n,'hidden'),'collision':n.get_actor_enable_collision(),'velocity':vec(n.get_velocity()),'traffic':n.get_component_by_class(unreal.YUFSLocalMovementComponent).get_traffic_diagnostics(),'audit':n.get_hazard_audit()})
 report['runs'][-1]['samples'].append(row);save();return row
def spawn(w,xyz,i):
 tr=unreal.Transform(location=unreal.Vector(*xyz));api=unreal.get_default_object(unreal.GameplayStatics)
 n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 if not n:return None
 n.set_editor_property('StableNPCId',1000+i);n.set_editor_property('enable_everyday_behavior',False)
 api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return n
def tick(dt):
 global case,stage,stopping,ending,restart,last,npcs,sim,volume,binary,cls,positions
 try:
  now=time.monotonic()
  if stopping:
   if level.is_in_play_in_editor():return
   if ending:
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
   if restart is None:restart=now;return
   if now-restart<1:return
   case+=1;stage='setup';stopping=False;npcs=[];last=-1;level.editor_request_begin_play();return
  if now-began>390:raise RuntimeError('Three-scenario run exceeded390s')
  w=editor.get_game_world()
  if not w:return
  if stage=='setup':
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume);binary=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSBinaryManager)
   palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget));cls=field(field(palette,'available_np_cs')[0],'npc_class')
   sim.select_fire_option(case)
   if positions is None:
    candidates=[(x,y,350) for y in [-300,-500,-650] for x in [1100,1400,1700,2000]]+[(x,y,5) for y in [-900,-1020] for x in [750,900,1050,1200,1350,1500,1650,1800,1950,2100,2250]]
    positions=[]
    for xyz in candidates:
     p=unreal.NavigationSystemV1.project_point_to_navigation(w,unreal.Vector(*xyz),None,None,unreal.Vector(35,35,45))
     if p is None or abs(p.z-xyz[2])>45:continue
     body=p+unreal.Vector(0,0,90)
     if any((body-unreal.Vector(*old)).length()<85 for old in positions):continue
     n=spawn(w,vec(body),len(npcs))
     if not n:continue
     positions.append(vec(n.get_actor_location()));npcs.append(n)
     if len(npcs)==20:break
   else:
    for i,xyz in enumerate(positions):
     n=spawn(w,xyz,i);assert n is not None,'Identical saved starting point no longer clear';npcs.append(n)
   assert len(npcs)==20,'Could not place20 supported non-overlapping NPCs'
   options=field(sim,'fire_options');opt=options[case]
   report['runs'].append({'case':case,'option':str(field(opt,'DisplayName')),'initial':[vec(n.get_actor_location()) for n in npcs],'samples':[]})
   sim.set_editor_property('fire_start_delay_seconds',0);sample('before_start');sim.start_simulation();stage='run'
  elif stage=='run':
   if int(sim.get_elapsed_time())!=last:last=int(sim.get_elapsed_time());sample('live')
   if sim.is_timeline_review_mode():
    row=sample('final_review');assert row['total']==20 and row['evacuated']+row['incap']<=20
    assert row['frame']>=470,'Fire did not progress to normal60s endpoint'
    assert any('Ready' in r['binary'] for r in report['runs'][-1]['samples']),'No ready BIN samples'
    assert all(all(math.isfinite(v) for v in n['xyz']) for n in row['npcs'])
    stored=sim.get_all_run_results();assert len(stored)==1
    result=stored[0];assert field(result,'EvacuatedCount')==row['evacuated'] and field(result,'IncapacitatedCount')==row['incap']
    report['runs'][-1]['result']={'evacuated':row['evacuated'],'incap':row['incap'],'unresolved':20-row['evacuated']-row['incap'],'frame':row['frame'],'elapsed':row['time']}
    assert all(r['evacuated']<=row['evacuated'] and r['incap']<=row['incap'] for r in report['runs'][-1]['samples'])
    stopping=True;ending=case==1;restart=None
    if ending:report['complete']=True
    save();level.editor_request_end_play()
 except Exception:
  report['error']=traceback.format_exc();save();ending=True;stopping=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
