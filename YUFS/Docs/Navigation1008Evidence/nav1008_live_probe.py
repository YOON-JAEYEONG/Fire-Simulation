import unreal, json, time, traceback
from pathlib import Path
out = Path(__file__).with_suffix('.json')
report = {'mode': 'Rendered Main PIE, three separate fire runs; palette class, temporary nav-projected NPCs; only runtime fire delay set to zero', 'runs': []}
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
level.load_level('/Game/Maps/Main')
option = 0
prepared = False
ending = False
stopping = False
restart_at = None
started = time.monotonic()
last = -1
npcs = []

def save(): out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')

def tick(dt):
    global option, prepared, ending, stopping, restart_at, last, npcs, started, sim, bm, comp
    try:
        if stopping:
            if level.is_in_play_in_editor(): return
            if ending:
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
                return
            if restart_at is None: restart_at = time.monotonic(); return
            if time.monotonic()-restart_at < 1: return
            option += 1
            prepared = False
            stopping = False
            restart_at = None
            last = -1
            npcs = []
            started = time.monotonic()
            level.editor_request_begin_play()
            return
        w = editor.get_game_world()
        if not w:
            if time.monotonic()-started > 120: raise RuntimeError('PIE timeout')
            return
        if not prepared:
            sim = unreal.GameplayStatics.get_actor_of_class(w, unreal.YUFSSimulationController)
            bm = unreal.GameplayStatics.get_actor_of_class(w, unreal.YUFSBinaryManager)
            volume = unreal.GameplayStatics.get_actor_of_class(w, unreal.YUFSHeterogeneousVolume)
            comp = volume.get_component_by_class(unreal.HeterogeneousVolumeComponent)
            sim.select_fire_option(option)
            widgets = unreal.WidgetLibrary.get_all_widgets_of_class(w, unreal.UserWidget, False)
            palette = next(a for a in widgets if isinstance(a, unreal.YUFSNPCPaletteWidget))
            cls = palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
            half = unreal.get_default_object(cls).capsule_component.get_scaled_capsule_half_height()
            for floor in [unreal.Vector(1200,-300,350),unreal.Vector(1200,-300,0),unreal.Vector(3050,-1137,350),unreal.Vector(2150,-800,0)]:
                p = unreal.NavigationSystemV1.project_point_to_navigation(w, floor, None, None, unreal.Vector(80,80,70))
                if p is None: continue
                tr = unreal.Transform(location=p+unreal.Vector(0,0,half+2))
                n = unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                if n:
                    unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                    npcs.append(n)
            report['runs'].append({'option': option, 'count': len(npcs), 'class': cls.get_path_name(), 'samples': []})
            sim.set_editor_property('fire_start_delay_seconds',0)
            sim.start_simulation()
            started = time.monotonic()
            prepared = True
        seconds = int(time.monotonic()-started)
        if seconds != last:
            last = seconds
            report['runs'][-1]['samples'].append({'t': seconds, 'sim_time': sim.get_elapsed_time(), 'phase': str(sim.get_current_phase()), 'visual_frame': comp.get_editor_property('frame'), 'diagnostics': bm.get_hazard_diagnostics(), 'npcs': [n.get_hazard_audit() for n in npcs]})
            save()
            if seconds >= 25:
                stopping = True
                ending = option >= 2
                level.editor_request_end_play()
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        ending = True
        stopping = True
        level.editor_request_end_play()

handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
