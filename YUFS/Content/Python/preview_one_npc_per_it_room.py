"""Run a one-NPC-per-room preview of the final IT building.

Launch Unreal Editor with -ExecutePythonScript pointing to this file after
building YUFSEditor. The script opens the simulation, places NPCs in reachable
room interiors, and waits at the existing Start button. It never saves the
level. The placement list is written to Saved/OneNpcPerItRoomPreview.json.
"""
import json
import time
import traceback
from collections import Counter, defaultdict, deque
from pathlib import Path
import unreal as ue

MAP = '/Game/Maps/it관_찐최종/Main_level'
MENU = '/Game/Maps/Lvl_MainMenu'
CLASSPATH = '/Game/Blueprint/BP_YUFSRLEvacuationNPC.BP_YUFSRLEvacuationNPC_C'
level = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
editor = ue.get_editor_subsystem(ue.UnrealEditorSubsystem)
launched = False
began = time.monotonic()
STEP = 45


def read(p,name):
    return p.get_editor_property(name)


def prepare(world):
    cls = ue.load_class(None,CLASSPATH)
    manager = ue.GameplayStatics.get_actor_of_class(world,ue.YUFSLevelDataManager)
    assert manager and hasattr(manager,'has_connected_exit_from'), 'The checked build is required'
    plan = json.loads((Path(ue.Paths.project_content_dir())/'Python/MergeFinalDoorPlan.json').read_text(encoding='utf8'))
    diag = ue.YUFSMapDiagnostics
    cache = {}
    rooms = []
    rejected = []
    missing_seed = []
    gates = Counter()
    for z in (20,370):
        floor = 1 if z==20 else 2
        def probe(x,y):
            key = (x,y,z)
            if key not in cache:
                p = diag.probe_location(world,ue.Vector(x,y,z),cls,48.0)
                cache[key] = (read(p,'floor_location'),read(p,'navigation_location'),
                              read(p,'on_navigation')) if read(p,'supported') and read(p,'body_clear') else None
            return cache[key]
        def clear(a,b):
            if abs(a.z-b.z)>35: return False
            for height in (60,135):
                h = ue.Vector(0,0,height)
                if ue.SystemLibrary.line_trace_single(world,a+h,b+h,ue.TraceTypeQuery.TRACE_TYPE_QUERY1,
                    False,[],ue.DrawDebugTrace.NONE,True):
                    return False
            return True
        for side,ywall,yrange in (('North',-8793.75,(-10040,-8840)),('South',-8553.75,(-8510,-7350))):
            direction = -1 if side=='North' else 1
            seeds = []
            tagged = [d for d in plan if d['floor']==z and d['side']==side]
            for i,d in enumerate(tagged):
                seeds.append((f'door-{i+1}',[(d['x0']+(d['x1']-d['x0'])*p,ywall+direction*depth)
                    for depth in (165,215,255,120) for p in (.50,.32,.68)]))
            strips = []
            for x in range(2470,9480,15):
                if 5540<x<6350 or any(d['x0']-25<=x<=d['x1']+25 for d in tagged): continue
                passes = [probe(x,ywall+dy) for dy in (-125,-65,0,65,125)]
                if not all(passes) or not all(clear(a[0],b[0]) for a,b in zip(passes,passes[1:])): continue
                if strips and x-strips[-1][-1]==15: strips[-1].append(x)
                else: strips.append([x])
            for i,xs in enumerate(strips):
                if len(xs)*15>360: continue  # Open lobby/landing, not a room entrance.
                ordered=sorted(xs,key=lambda x:abs(x-sum(xs)/len(xs)))
                seeds.append((f'open-{i+1}',[(x,ywall+direction*d) for d in (165,215,260,120) for x in ordered]))
            nodes = {}
            for x in range(2400,9585,STEP):
                for y in range(*yrange,STEP):
                    p = probe(x,y)
                    if p: nodes[(x,y)] = p
            cell_ix = {}
            components = []
            for root in nodes:
                if root in cell_ix: continue
                number = len(components)
                section = []
                components.append(section)
                cell_ix[root] = number
                queue = deque([root])
                while queue:
                    a,b = queue.popleft()
                    section.append((a,b))
                    for n in ((a+STEP,b),(a-STEP,b),(a,b+STEP),(a,b-STEP)):
                        if n in cell_ix or n not in nodes or not clear(nodes[(a,b)][0],nodes[n][0]):
                            continue
                        cell_ix[n] = number
                        queue.append(n)
            assigned = defaultdict(list)
            gate_points = defaultdict(list)
            for name,options in seeds:
                group = None
                for x,y in options:
                    p = probe(x,y)
                    if not p: continue
                    closest = sorted((k for k in nodes if abs(k[0]-x)<=60 and abs(k[1]-y)<=60),
                        key=lambda k:(k[0]-x)**2+(k[1]-y)**2)
                    group = next((cell_ix[k] for k in closest if clear(p[0],nodes[k][0])),None)
                    if group is not None:
                        assigned[group].append(name)
                        if p[2]: gate_points[group].append(p[1])
                        break
                if group is None and name.startswith('door-'):
                    missing_seed.append((floor,side,name,options[0]))
                if group is not None: gates[(floor,name.split('-')[0])] += 1
            for group,names in assigned.items():
                section = components[group]
                cx = sum(k[0] for k in section)/len(section)
                cy = sum(k[1] for k in section)/len(section)
                ranked = sorted((k for k in section if nodes[k][2]),
                    key=lambda k:(k[0]-cx)**2+(k[1]-cy)**2
                         + max(0,185-abs(k[1]-ywall))**2*2)
                candidates = [nodes[k][1] for k in ranked] + gate_points[group]
                pos = next((p for p in candidates if manager.has_connected_exit_from(p,cls)),None)
                if pos is None:
                    rejected.append(dict(floor=floor,side=side,gates=names,points=len(section)))
                    continue
                rooms.append((floor,side,names,pos))
    cdo=ue.get_default_object(cls)
    half=cdo.capsule_component.get_scaled_capsule_half_height()
    gs=ue.get_default_object(ue.GameplayStatics)
    placements=[]
    for floor,side,names,p in rooms:
        t=ue.Transform(location=p+ue.Vector(0,0,half+2))
        actor=gs.call_method('BeginDeferredActorSpawnFromClass',(world,cls,t,
            ue.SpawnActorCollisionHandlingMethod.DONT_SPAWN_IF_COLLIDING,None,ue.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        assert actor, 'Collision while placing '+str((floor,side,names,p))
        gs.call_method('FinishSpawningActor',(actor,t,ue.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        placements.append(dict(floor=floor,side=side,gates=names,npc=actor.get_name(),xyz=[p.x,p.y,p.z]))
    count=Counter(row['floor'] for row in placements)
    assert count[1] and count[2], str(count)
    assert not missing_seed, str(missing_seed)
    return dict(placements=placements,byFloor=dict(count),rejected=rejected,
                gates={f'{k[0]}F-{k[1]}':v for k,v in gates.items()})


def tick(_):
    global launched
    try:
        world = editor.get_game_world()
        if not world:
            return
        if not launched:
            cfg = ue.YUFSScenarioConfig()
            cfg.set_editor_property('simulation_map',ue.load_object(None,MAP+'.Main_level'))
            cfg.set_editor_property('policy_type',ue.YUFSScenarioPolicy.RULE_BASED)
            cfg.set_editor_property('fire_option_index',2)
            cfg.set_editor_property('fire_start_delay_seconds',0.)
            cfg.set_editor_property('npc_count',0)
            cfg.set_editor_property('timeline_record_end_fire_seconds',100.)
            ue.GameplayStatics.get_game_instance(world).launch_scenario(cfg)
            launched = True
            return
        if not world.get_name().endswith('Main_level'):
            assert time.monotonic()-began<100, 'IT level did not load'
            return
        sim = ue.GameplayStatics.get_actor_of_class(world,ue.YUFSSimulationController)
        if not sim or time.monotonic()-began<5:
            return
        result = prepare(world)
        player = ue.GameplayStatics.get_player_controller(world,0)
        cameras = ue.GameplayStatics.get_all_actors_with_tag(world,'OverviewCamera')
        if player and cameras:
            player.set_view_target_with_blend(cameras[0],0.5)
        result['phase'] = str(sim.get_current_phase())
        out = Path(ue.Paths.project_saved_dir())/'OneNpcPerItRoomPreview.json'
        out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
        ue.log('[ONE_PER_ROOM] Ready at Start button; byFloor='+str(result['byFloor'])
            +' gates='+str(result['gates'])+' rejected='+str(result['rejected']))
        ue.unregister_slate_post_tick_callback(handle)
    except Exception:
        ue.log_error('[ONE_PER_ROOM] '+traceback.format_exc())
        ue.unregister_slate_post_tick_callback(handle)


assert level.load_level(MENU)
ue.EditorPythonScripting.set_keep_python_script_alive(True)
handle = ue.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
