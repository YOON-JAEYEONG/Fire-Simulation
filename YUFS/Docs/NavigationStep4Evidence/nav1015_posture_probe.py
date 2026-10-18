import unreal, json, time, traceback
from pathlib import Path
out = Path(__file__).with_suffix('.json')
report = {'mode': 'Main PIE posture verification; fire1 fixed frame480. Two stationary palette NPCs: walking movement mode with movement component tick disabled, external navigation, exposure scale3/1. Third NPC moves normally with temporary crawl threshold0.04 solely to exercise low locomotion. No asset changes.', 'samples': []}
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
level.load_level('/Game/Maps/Main')
started = time.monotonic()
prepared = False
aligned = False
ending = False
last = -1
npcs = []

def save(): out.write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
def tick(dt):
    global prepared, aligned, ending, started, last, sim, volume, bm, npcs
    try:
        if ending:
            if not level.is_in_play_in_editor():
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
            return
        w=editor.get_game_world()
        if not w: return
        if not prepared:
            sim=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSSimulationController)
            volume=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSHeterogeneousVolume)
            bm=unreal.GameplayStatics.get_actor_of_class(w,unreal.YUFSBinaryManager)
            palette=next(a for a in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False) if isinstance(a,unreal.YUFSNPCPaletteWidget))
            cls=palette.get_editor_property('available_np_cs')[0].get_editor_property('npc_class')
            half=unreal.get_default_object(cls).capsule_component.get_scaled_capsule_half_height()
            for scale,floor in [(3,unreal.Vector(1200,-300,350)),(1,unreal.Vector(1200,-450,350)),(3,unreal.Vector(1600,-400,350))]:
                tr=unreal.Transform(location=floor+unreal.Vector(0,0,half+2))
                n=unreal.get_default_object(unreal.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(w,cls,tr,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                unreal.get_default_object(unreal.GameplayStatics).call_method('FinishSpawningActor',(n,tr,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
                n.set_editor_property('enable_everyday_behavior',False)
                n.set_editor_property('use_external_navigation_driver',len(npcs)<2)
                n.character_movement.set_movement_mode(unreal.MovementMode.MOVE_WALKING)
                if len(npcs)<2: n.character_movement.set_component_tick_enabled(False)
                mind=n.get_component_by_class(unreal.YUFSBehaviorStateMachine)
                cfg=unreal.YUFSBehaviorConfig()
                cfg.set_editor_property('smoke_exposure_time_scale',scale)
                if len(npcs)==2: cfg.set_editor_property('crawl_threshold',0.04); cfg.set_editor_property('crawl_recovery_threshold',0.02)
                mind.set_editor_property('config',cfg)
                npcs.append(n)
            sim.set_editor_property('fire_start_delay_seconds',0)
            sim.start_simulation()
            volume.set_frame(480)
            started=time.monotonic(); prepared=True
        if not aligned:
            if sim.get_current_phase() != unreal.SimPhase.FIRE_ACTIVE: return
            volume.set_frame(480)
            started=time.monotonic()
            aligned=True
            return
        if volume.get_frame() != 480: raise RuntimeError('Controlled exposure playback did not stay at frame 480')
        seconds=int(time.monotonic()-started)
        if seconds!=last:
            last=seconds
            report['samples'].append({'t':seconds,'sim_time':sim.get_elapsed_time(),'diagnostics':bm.get_hazard_diagnostics(),'npcs':[{'name':n.get_name(),'audit':n.get_hazard_audit(),'animation':n.get_component_by_class(unreal.YUFSActionAnimationComponent).get_active_animation_name(),'native_compatible':n.get_component_by_class(unreal.YUFSActionAnimationComponent).can_use_native_animations(n.mesh),'max_speed':n.character_movement.max_walk_speed,'max_crouched_speed':n.character_movement.max_walk_speed_crouched,'capsule_half':n.capsule_component.get_scaled_capsule_half_height(),'radius':n.capsule_component.get_scaled_capsule_radius(),'breathing_height':n.get_breathing_height_cm(),'xyz':[n.get_actor_location().x,n.get_actor_location().y,n.get_actor_location().z],'velocity':(n.get_velocity().x**2+n.get_velocity().y**2+n.get_velocity().z**2)**0.5,'mesh_relative_z':n.mesh.get_editor_property('relative_location').z} for n in npcs]})
            save()
            if seconds>=45: ending=True; level.editor_request_end_play()
    except Exception:
        report['error']=traceback.format_exc();save();ending=True;level.editor_request_end_play()
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
