import unreal, json, time, traceback, os
from pathlib import Path
args = unreal.SystemLibrary.get_command_line()
def arg(key, default):
    import re
    m = re.search(r'-'+key+r'=([^ ]+)', args)
    return m.group(1) if m else default
run_id=arg('WalkRun','a'); seed=int(arg('WalkSeed','2026')); fps=int(arg('FPS','60'))
out=Path(__file__).parent/(run_id+'.json')
report={'run':run_id,'seed':seed,'fps':fps,'map':'/Game/Maps/Main','samples':[],'complete':False,'pid':os.getpid()}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
report['stage']='load_map';out.write_text(json.dumps(report),encoding='utf-8')
level.load_level('/Game/Maps/Main')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state='setup'; began=time.monotonic(); warm=0; npcs=[]; target=1; ending=False

def save(): out.write_text(json.dumps(report,indent=2),encoding='utf-8')
def vec(v): return [v.x,v.y,v.z]
def sample():
    return [{'id':n.get_editor_property('stable_npc_id'),'position':vec(n.get_actor_location()),
             'rotation':[n.get_actor_rotation().pitch,n.get_actor_rotation().yaw,n.get_actor_rotation().roll],
             'velocity':vec(n.get_velocity())}
            for n in npcs]
def tick(dt):
    global state,warm,npcs,sim,target,ending
    try:
        if ending:
            if not level.is_in_play_in_editor():
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
            return
        if time.monotonic()-began>180: raise RuntimeError('180 second timeout')
        w=editor.get_game_world()
        if not w:return
        if state=='setup':
            sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController)
            if not sim:return
            warm+=1
            if warm<120:return
            report['stage']='clear_npcs';save()
            for n in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.YUFSEvacuationNPC):n.destroy_actor()
            report['stage']='palette';save()
            palettes=unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.YUFSNPCPaletteWidget,False)
            cls=palettes[0].get_editor_property('available_np_cs')[0].get_editor_property('npc_class') if palettes else unreal.YUFSEvacuationNPC
            report['npc_class']=str(cls)
            api=unreal.get_default_object(unreal.GameplayStatics)
            for i,xyz in enumerate([(1100,-300,350),(1400,-450,350),(1700,-650,350),(2000,-500,350)]):
                report['stage']='project_'+str(i);save()
                floor=unreal.Vector(*xyz) # Authored upstairs floor coordinates used by existing Main probes.
                report['stage']='spawn_'+str(i);save()
                tr=unreal.Transform(location=floor+unreal.Vector(0,0,90),rotation=unreal.Rotator())
                n=api.call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                n.set_editor_property('stable_npc_id',100+i)
                api.call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                npcs.append(n)
            sim.set_editor_property('fixed_random_seed',seed)
            sim.set_editor_property('fire_start_delay_seconds',120)
            report['stage']='start';save()
            sim.start_simulation();report['initial']=sample();save();state='run'
        elif state=='run':
            elapsed=sim.get_elapsed_time()
            if round(elapsed*fps)>=target*fps:
                report['samples'].append({'second':target,'time':elapsed,'npcs':sample()})
                target+=1;save()
            if target>20:
                report['complete']=True;save();ending=True;level.editor_request_end_play()
    except Exception:
        report['error']=traceback.format_exc();save();ending=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
