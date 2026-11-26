"""PIE regression for connections through the actual final IT doorways.

Run the editor with -ExecutePythonScript pointing here. Results are written to
Saved/FinalITRoomConnections.json (YUFS_ROOM_NAV_REPORT overrides the name).
Each candidate must cross its own doorway, not reach the corridor by another door.
Use YUFS_ROOM_NAV_UPPER_GRID=1 to include unmarked upstairs openings;
YUFS_ROOM_NAV_RES=4,3 is an in-memory comparison and does not save the map.
"""

import json
import os
import time
import traceback
from pathlib import Path

import unreal as ue

MAP = '/Game/Maps/it관_찐최종/Main_level'
MENU = '/Game/Maps/Lvl_MainMenu'


def xyz(p):
    return [round(p.x, 2), round(p.y, 2), round(p.z, 2)]


def prop(obj, key):
    return obj.get_editor_property(key)


def crosses(points, row):
    for a, b in zip(points, points[1:]):
        if a.y == b.y or (a.y-row['y'])*(b.y-row['y']) > 0:
            continue
        alpha = (row['y']-a.y)/(b.y-a.y)
        x, z = a.x+(b.x-a.x)*alpha, a.z+(b.z-a.z)*alpha
        if row['x0']-10 <= x <= row['x1']+10 and abs(z-row['floor']) <= 75:
            return True
    return False


def collect(world):
    plan = json.loads((Path(ue.Paths.project_content_dir())/'Python/MergeFinalDoorPlan.json').read_text(encoding='utf8'))
    npc = ue.load_class(None, '/Game/Blueprint/BP_YUFSRLEvacuationNPC.BP_YUFSRLEvacuationNPC_C')
    probe = ue.YUFSMapDiagnostics
    exits = ue.GameplayStatics.get_all_actors_of_class(world, ue.YUFSExitPoint)
    doors = ue.GameplayStatics.get_all_actors_of_class(world, ue.YUFSInteractionDoor)
    results = []
    for index, row in enumerate(plan):
        side = -1 if row['side'] == 'North' else 1
        info = dict(id=index, floor=row['floor'], side=row['side'], doorY=row['y'], samples=[])
        for relative_x in (.50, .32, .68):
            x = row['x0']+(row['x1']-row['x0'])*relative_x
            a, b = [ue.Vector(x, row['y']+side*d, row['floor']) for d in (175,-115)]
            room, corridor = [probe.probe_location(world, p, npc, 50.) for p in (a,b)]
            result = dict(room=xyz(a), corridor=xyz(b))
            for name, item in [('room',room),('corridor',corridor)]:
                result[name+'Ground'] = xyz(prop(item,'floor_location'))
                result[name+'Nav'] = xyz(prop(item,'navigation_location'))
                for key in ('supported', 'body_clear', 'on_navigation', 'reason'):
                    result[name+'_'+key] = prop(item,key)
            if result['room_supported'] and result['corridor_supported']:
                start, end = prop(room,'floor_location'), prop(corridor,'floor_location')
                path = probe.probe_route(world, start, end)
                points = list(prop(path,'floor_points'))
                result['complete'] = prop(path,'complete')
                result['crossesOwnDoor'] = result['complete'] and crosses(points,row)
                result['points'] = [xyz(p) for p in points]
                if result['crossesOwnDoor'] and result['room_on_navigation']:
                    result['reachableExits'] = [e.get_name() for e in exits
                        if prop(probe.probe_route(world,start,e.get_actor_location()),'complete')]
            info['samples'].append(result)
        info['pass'] = any(s.get('crossesOwnDoor') and s.get('reachableExits')
            and s['room_on_navigation'] and s['corridor_on_navigation'] for s in info['samples'])
        results.append(info)
    navs = []
    for nav in ue.GameplayStatics.get_all_actors_of_class(world, ue.RecastNavMesh):
        navs.append(dict(name=nav.get_name(),settings=[
            [prop(res,key) for key in ('cell_size','cell_height','agent_max_step_height')]
            for res in prop(nav,'nav_mesh_resolution_params')]))
    result = dict(world=world.get_name(), doors=len(doors), exits=len(exits),
        passed=sum(d['pass'] for d in results), total=len(results), nav=navs, probes=results)
    if os.environ.get('YUFS_ROOM_NAV_UPPER_GRID') == '1':
        result['upper'] = scan_upper(world, probe, npc, plan)
    return result


def scan_upper(world, probe, npc, plan):
    # Scan actual supported floor on both sides of the two upstairs corridor
    # walls. The regular samples also catch small rooms with no tagged door.
    goal = ue.Vector(4934, -8670, 390)
    coords, openings = [], []
    for x in range(2290, 9320, 85):
        for y in range(-10140, -7130, 85):
            p = probe.probe_location(world, ue.Vector(x,y,380), npc, 50.)
            if not prop(p,'supported') or not prop(p,'body_clear'):
                continue
            floor = prop(p,'floor_location')
            known = prop(p,'on_navigation')
            complete = known and prop(probe.probe_route(world,floor,goal),'complete')
            coords.append([round(floor.x),round(floor.y),round(floor.z),known,complete,str(prop(p,'floor_actor'))])
    for row_y in (-8793.75,-8553.75):
        for x in range(2350,9250,20):
            if any(d['floor']==370 and abs(d['y']-row_y)<1 and d['x0']-32<=x<=d['x1']+32 for d in plan):
                continue
            checks = [probe.probe_location(world,ue.Vector(x,row_y+delta,380),npc,50.)
                      for delta in (0,-35,35,-70,70,-110,110)]
            if not all(prop(p,'supported') and prop(p,'body_clear') for p in checks):
                continue
            a,b = [prop(p,'floor_location') for p in checks[-2:]]
            path = probe.probe_route(world,a,b)
            points = list(prop(path,'floor_points'))
            direct = prop(path,'complete') and crosses(points,dict(y=row_y,x0=x-70,x1=x+70,floor=380))
            openings.append([x,row_y,xyz(a),xyz(b),bool(direct),bool(prop(path,'complete'))])
    floors = {p[5] for p in coords if p[3] and not p[4]}
    labels = {}
    for a in ue.GameplayStatics.get_all_actors_of_class(world, ue.StaticMeshActor):
        if a.get_name() in floors:
            comp = a.get_editor_property('static_mesh_component')
            mesh = comp.get_editor_property('static_mesh')
            origin,extent = a.get_actor_bounds(True)
            labels[a.get_name()] = dict(label=a.get_actor_label(), mesh=mesh.get_path_name() if mesh else '',
                center=xyz(origin), extent=xyz(extent))
    return dict(goal=xyz(goal), grid=coords, bareOpenings=openings, isolatedFloorActors=labels)


def main():
    level = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    editor = ue.get_editor_subsystem(ue.UnrealEditorSubsystem)
    report = Path(ue.Paths.project_saved_dir()) / os.environ.get('YUFS_ROOM_NAV_REPORT','FinalITRoomConnections.json')
    resolution = os.environ.get('YUFS_ROOM_NAV_RES')
    menu = os.environ.get('YUFS_ROOM_NAV_DIRECT') != '1' and not resolution
    if os.environ.get('YUFS_ROOM_NAV_SKIPLOAD') != '1':
        assert level.load_level(MENU if menu else MAP)
    ue.EditorPythonScripting.set_keep_python_script_alive(True)
    stopping = launched = False
    started = time.monotonic()
    map_seen = None
    baking = bool(resolution)
    if baking:
        size,height = [float(n) for n in resolution.split(',')]
        actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
        nav_meshes = [a for a in actors.get_all_level_actors() if isinstance(a,ue.RecastNavMesh)]
        assert nav_meshes, 'No Recast actor to measure'
        for nav in nav_meshes:
            params = prop(nav,'nav_mesh_resolution_params')
            for i,res in enumerate(params):
                res.set_editor_property('cell_size',size)
                res.set_editor_property('cell_height',height)
                params[i] = res
            nav.set_editor_property('nav_mesh_resolution_params',params)
        ue.SystemLibrary.execute_console_command(editor.get_editor_world(),'RebuildNavigation')

    def tick(_dt):
        nonlocal stopping, launched, map_seen, baking, started
        if stopping:
            if not level.is_in_play_in_editor():
                ue.unregister_slate_post_tick_callback(handle)
                ue.SystemLibrary.quit_editor()
            return
        try:
            assert time.monotonic()-started < 95, 'PIE map was unavailable'
            if baking:
                if time.monotonic()-started<1.0 or ue.NavigationSystemV1.is_navigation_being_built(editor.get_editor_world()):
                    return
                baking = False
                started = time.monotonic()
                level.editor_request_begin_play()
                return
            world = editor.get_game_world()
            if not world:
                return
            if menu and not launched:
                cfg = ue.YUFSScenarioConfig()
                cfg.set_editor_property('simulation_map',ue.load_object(None,MAP+'.Main_level'))
                cfg.set_editor_property('npc_count',0)
                ue.GameplayStatics.get_game_instance(world).launch_scenario(cfg)
                launched = True
                return
            if not world.get_name().endswith('Main_level'):
                return
            if map_seen is None:
                map_seen = time.monotonic()
            if time.monotonic()-map_seen<5.0:
                return
            result = collect(world)
            result['passesRegression'] = result['passed']==result['total']
            if 'upper' in result:
                openings = result['upper']['bareOpenings']
                result['passesRegression'] &= bool(openings) and all(p[4] for p in openings)
            report.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
            ue.log('[IT_ROOM_NAV] '+str({k:v for k,v in result.items() if k not in ('probes','upper')}))
            if 'upper' in result:
                g = result['upper']['grid']
                o = result['upper']['bareOpenings']
                ue.log('[IT_ROOM_NAV_UPPER] '+str(dict(points=len(g),noNav=sum(not p[3] for p in g),
                    isolated=sum(p[3] and not p[4] for p in g),openingSamples=len(o),
                    disconnectedOpeningSamples=sum(not p[4] for p in o))))
        except Exception:
            result = {'error':traceback.format_exc()}
            report.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
            ue.log_error('[IT_ROOM_NAV] '+result['error'])
        stopping = True
        level.editor_request_end_play()

    handle = ue.register_slate_post_tick_callback(tick)
    if not baking:
        level.editor_request_begin_play()


if __name__=='__main__':
    main()
