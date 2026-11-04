import unreal,json,time,traceback,re,struct
from pathlib import Path
out=Path(__file__).with_suffix('.json');r={'mode':'Rendered final-map PIE; paused frame seeks and fixed physical NPC eye probes. No assets saved.','samples':[]}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);ed=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/it관_찐최종/Main_level');unreal.EditorPythonScripting.set_keep_python_script_alive(True)
done=False;ready=None;started=time.monotonic();job=0;selected=None;seek=None;npcs=[]
jobs=[(i,t) for i in (0,1,2,0) for t in (0,100,480,900,999999)]
def f(o,k):return o.get_editor_property(k)
def vec(v):return [v.x,v.y,v.z]
def save():out.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
def tick(dt):
 global done,ready,job,selected,seek,sim,bm,volume,npcs
 try:
  if done:
   if not level.is_in_play_in_editor():
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
   return
  if time.monotonic()-started>180:raise RuntimeError('Data probe timeout')
  w=ed.get_game_world()
  if not w:return
  if ready is None:ready=time.monotonic();return
  if time.monotonic()-ready<1:return
  if not npcs:
   sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController);bm=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSBinaryManager);volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
   palette=unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.YUFSNPCPaletteWidget,False)[0];cls=f(f(palette,'available_np_cs')[0],'npc_class')
   for x,y,z in [(3000,-8660,20),(4500,-8650,20),(6000,-8650,20),(7800,-8650,20),(9000,-8650,20),(3100,-8150,20),(3000,-8660,370),(4500,-8650,370),(6000,-8650,370),(7800,-8650,370),(9000,-8650,370),(8800,-8150,370)]:
    p=unreal.YUFSMapDiagnostics.probe_location(w,unreal.Vector(x+13,y+7,z),cls)
    assert f(p,'bSupported') and f(p,'bBodyClear') and f(p,'bOnNavigation'),str(p)
    tr=unreal.Transform(location=f(p,'NavigationLocation')+unreal.Vector(0,0,90));api=unreal.get_default_object(unreal.GameplayStatics)
    n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));assert n
    api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));n.set_actor_tick_enabled(False);n.character_movement.set_component_tick_enabled(False);npcs.append(n)
   r['fps']=volume.get_playback_frame_rate();r['settings']=[str(o) for o in f(sim,'fire_options')]
  option,wanted=jobs[job]
  if seek is None:
   if selected!=option:sim.select_fire_option(option);selected=option
   volume.set_frame(wanted);seek=time.monotonic();return
  if time.monotonic()-seek<.7:return
  diag=bm.get_hazard_diagnostics()
  if 'status=Loading' in diag and time.monotonic()-seek<12:return
  assert 'status=Ready' in diag,diag
  expected_count=995 if option==0 else 994
  assert f(volume,'total_frame_count')==expected_count
  assert volume.get_frame()==min(wanted,expected_count-1)
  row={'option':option,'requested':wanted,'frame':volume.get_frame(),'diagnostics':diag,'probes':[]}
  filename=f(f(sim,'fire_options')[option],'binary_file_path')
  file=Path(unreal.Paths.project_content_dir())/filename
  with file.open('rb') as raw:
   nf,x,y,z=struct.unpack('<4i',raw.read(16));count=x*y*z;raw.seek(16+volume.get_frame()*count*2);density=raw.read(count);heat=raw.read(count)
  for n in npcs:
   audit=n.get_hazard_audit();assert 'eyeStatus=Ready' in audit,audit
   idx=int(re.search(r'index=(-?\d+)',audit)[1]);actual=tuple(map(int,re.search(r'rawSmokeHeat=(\d+)/(\d+)',audit).groups()))
   assert actual==(density[idx],heat[idx]),str((option,volume.get_frame(),idx,actual))
   eye=list(map(float,re.search(r'eye=X=([\d.-]+) Y=([\d.-]+) Z=([\d.-]+)',audit).groups()))
   cell=list(map(float,re.search(r'cell=X=([\d.-]+) Y=([\d.-]+) Z=([\d.-]+)',audit).groups()))
   expect=[(eye[0]-2200)/50,(-eye[1]-7000)/50,(eye[2]+80)/50]
   assert all(abs(a-b)<.01 for a,b in zip(cell,expect)),str((cell,expect))
   row['probes'].append({'audit':audit,'raw_match':True,'cell_matches_world':True})
  r['samples'].append(row);save();job+=1;seek=None
  if job==len(jobs):
   assert any('eyeSmoke=0.0000' not in p['audit'] for s in r['samples'] for p in s['probes'])
   r['raw_checks']=sum(len(s['probes']) for s in r['samples']);r['complete']=True;save();done=True;level.editor_request_end_play()
 except Exception:r['error']=traceback.format_exc();save();done=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
