import unreal, json, time, traceback
from pathlib import Path

out = Path(__file__).with_suffix('.json')
report = {'mode': 'Rendered Main PIE; explicit paused frame seeks, fixed eye probes, no saved map changes', 'settings': [], 'samples': []}
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level.load_level('/Game/Maps/Main')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started = time.monotonic()
jobs = [(opt, frame) for opt in (0, 1, 2, 0) for frame in (0, 100, 480, 750, 996)]
job = 0
selected = None
seek_at = None
npcs = []
ending = False

def save():
    out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')

def tick(dt):
    global selected, seek_at, job, ending, npcs
    try:
        if ending:
            if not level.is_in_play_in_editor():
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
            return
        w = editor.get_game_world()
        if not w:
            if time.monotonic() - started > 180: raise RuntimeError('PIE start timeout')
            return
        sim = unreal.GameplayStatics.get_actor_of_class(w, unreal.YUFSSimulationController)
        bm = unreal.GameplayStatics.get_actor_of_class(w, unreal.YUFSBinaryManager)
        volume = unreal.GameplayStatics.get_actor_of_class(w, unreal.YUFSHeterogeneousVolume)
        comp = volume.get_component_by_class(unreal.HeterogeneousVolumeComponent)
        if not npcs:
            cls = unreal.load_class(None, '/Game/NPCs/Blueprints/BP_YUFSRLEvacuationNPC.BP_YUFSRLEvacuationNPC_C')
            if not cls:
                widgets = unreal.WidgetLibrary.get_all_widgets_of_class(w, unreal.UserWidget, False)
                palette = next(a for a in widgets if isinstance(a, unreal.YUFSNPCPaletteWidget))
                cls = palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
            report['npc_class'] = cls.get_path_name()
            half = unreal.get_default_object(cls).capsule_component.get_scaled_capsule_half_height()
            for floor in [unreal.Vector(1200,-300,350), unreal.Vector(1200,-300,0), unreal.Vector(3050,-1137,350), unreal.Vector(3050,-1137,0)]:
                tr = unreal.Transform(location=floor+unreal.Vector(0,0,half+2))
                n = unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass', (w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor', (n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                n.set_actor_tick_enabled(False)
                n.character_movement.set_movement_mode(unreal.MovementMode.MOVE_NONE)
                npcs.append(n)
            report['settings'] = [{'i': i, 'bin': o.get_editor_property('binary_file_path'), 'svt': o.get_editor_property('use_sparse_volume_texture_coordinates'), 'grid': str(o.get_editor_property('binary_grid_to_texture')), 'transform': str(o.get_editor_property('volume_transform')), 'material': str(o.get_editor_property('fire_material')), 'ratio': o.get_editor_property('binary_frames_per_visual_frame'), 'offset': o.get_editor_property('binary_frame_offset')} for i,o in enumerate(sim.get_editor_property('fire_options'))]
            report['anchor_option'] = sim.get_editor_property('hazard_grid_reference_fire_option')
            report['auto_transform'] = sim.get_editor_property('auto_apply_fire_volume_transform')
            report['fps'] = volume.get_playback_frame_rate()
        option, frame = jobs[job]
        if seek_at is None:
            if selected != option:
                sim.select_fire_option(option)
                selected = option
            volume.set_frame(frame)
            seek_at = time.monotonic()
            return
        if time.monotonic() - seek_at < 1.5: return
        diag = bm.get_hazard_diagnostics()
        if 'status=Loading' in diag and time.monotonic() - seek_at < 8: return
        report['samples'].append({'option': option, 'seek': frame, 'visual_frame': volume.get_frame(), 'component_frame': comp.get_editor_property('frame'), 'diagnostics': diag, 'volume_transform': str(volume.get_actor_transform()), 'probes': [n.get_hazard_audit() for n in npcs]})
        save()
        job += 1
        seek_at = None
        if job >= len(jobs):
            ending = True
            level.editor_request_end_play()
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        ending = True
        level.editor_request_end_play()

handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
