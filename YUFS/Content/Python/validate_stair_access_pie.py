"""PIE-only check that each staircase of Main (West x=40..340, Middle x=2000..2300 south, East x=3960..4260)
connects 2F to 1F for the navmesh agent, and which stairs the test NPCs actually walk down; saves nothing.

For each stair: synchronous path 2F landing -> 1F foot, and paths from 2F points near it to the nearest
end exit, reporting which stair box the path descends through. Then an ordinary Start with the test NPCs
and a 90 s observation that logs the first stair each NPC passes through (capsule centre at an
intermediate height inside a stair box) and the exits cleared.
Run: UnrealEditor-Cmd.exe YUFS.uproject -NullRHI -YUFSSpawnTestNPCs=20 -ExecutePythonScript=<this file>
"""
import time
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level('/Game/Maps/Main')
start = time.monotonic()
checked = False
started_sim = False
seen = {}          # npc name -> stair name
last_scan = 0.0
F1, F2 = 5.0, 355.0
# name: (xmin, xmax, ymin, ymax, 2F landing point, 1F foot point)
STAIRS = {
    'West':   (40, 340, -603, -100,   (300, -200, F2), (110, -60, F1)),
    'Middle': (2000, 2300, -1540, -1040, (2150, -1100, F2), (2070, -1000, F1)),
    'East':   (3960, 4260, -592, -100, (4190, -200, F2), (4030, -60, F1)),
}
# 2F probe points near each stair (hall and a room on the same side of the building)
PROBES = {
    'West':   [(520, -400, F2), (185, -1440, F2), (540, -1440, F2)],
    'Middle': [(2150, -400, F2), (2700, -1440, F2)],
    'East':   [(3800, -400, F2), (4125, -1440, F2), (3780, -1440, F2)],
}
NEAREST_EXIT = {'West': 'WestEnd', 'Middle': 'MainEntrance', 'East': 'EastEnd'}


def log(message):
    unreal.log('[STAIR_PIE] ' + message)


def stair_of(p, lo=30.0, hi=320.0):
    """Stair whose box contains point p at an intermediate height (feet between the storeys)."""
    for name, (x0, x1, y0, y1, _, _) in STAIRS.items():
        if x0 - 60 <= p.x <= x1 + 60 and y0 - 60 <= p.y <= y1 + 60 and lo <= p.z <= hi:
            return name
    return None


def find_path(world, a, b):
    path = unreal.NavigationSystemV1.find_path_to_location_synchronously(
        world, unreal.Vector(*a), unreal.Vector(*b) if isinstance(b, tuple) else b, None, None)
    ok = path is not None and path.is_valid() and not path.is_partial()
    points = list(path.path_points) if path is not None else []
    return ok, points, path


def describe(points):
    used = []
    for p in points:
        s = stair_of(p)
        if s and s not in used:
            used.append(s)
    zs = [p.z for p in points]
    return used, (min(zs) if zs else None), (max(zs) if zs else None)


def check_paths(world):
    exit_class = unreal.load_class(None, '/Script/YUFS.YUFSExitPoint')
    targets = {}
    for exit_actor in unreal.GameplayStatics.get_all_actors_of_class(world, exit_class):
        targets[str(exit_actor.get_editor_property('ExitID'))] = exit_actor.get_navigation_destination()
    log('exit targets: ' + ', '.join('%s=%s' % kv for kv in targets.items()))
    for name, (x0, x1, y0, y1, top, foot) in STAIRS.items():
        ok, points, path = find_path(world, top, foot)
        used, zlo, zhi = describe(points)
        log('%s stair 2F->1F: complete=%s partial=%s points=%d z=%s..%s via=%s' % (
            name, ok, path.is_partial() if path else None, len(points), zlo, zhi, used))
        if not ok:
            unreal.log_warning('[STAIR_PIE] %s stair has NO complete 2F->1F path' % name)
        target = targets.get(NEAREST_EXIT[name])
        for probe in PROBES[name]:
            ok, points, path = find_path(world, probe, target)
            used, zlo, zhi = describe(points)
            length = sum(unreal.Vector.distance(points[i], points[i + 1]) for i in range(len(points) - 1)) if points else 0
            log('%s 2F probe %s -> %s: complete=%s partial=%s points=%d length=%.0f via=%s' % (
                name, probe, NEAREST_EXIT[name], ok, path.is_partial() if path else None, len(points), length, used))


def tick(dt):
    global checked, started_sim, last_scan
    elapsed = time.monotonic() - start
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if world and not checked and elapsed > 5:
            checked = True
            npc_class = unreal.load_class(None, '/Script/YUFS.YUFSEvacuationNPC')
            npcs = unreal.GameplayStatics.get_all_actors_of_class(world, npc_class)
            log('npcCount=%d' % len(npcs))
            for npc in npcs:
                log('spawn %s at %s' % (npc.get_name(), npc.get_actor_location()))
            check_paths(world)
            controllers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.load_class(None, '/Script/YUFS.YUFSSimulationController'))
            if controllers:
                controllers[0].start_simulation()
                started_sim = True
                log('ordinary Start invoked at %.1f s' % elapsed)
        if world and started_sim and elapsed - last_scan > 0.2:
            last_scan = elapsed
            npc_class = unreal.load_class(None, '/Script/YUFS.YUFSEvacuationNPC')
            for npc in unreal.GameplayStatics.get_all_actors_of_class(world, npc_class):
                name = npc.get_name()
                if name in seen:
                    continue
                p = npc.get_actor_location()
                s = stair_of(p, 130.0, 400.0)      # capsule centre: ~96 cm above the feet
                if s:
                    seen[name] = s
                    log('npc %s on %s stair at %.1f s pos=%s' % (name, s, elapsed, p))
        if elapsed > 100:
            counts = {}
            for s in seen.values():
                counts[s] = counts.get(s, 0) + 1
            log('observation complete; stair use by NPC: %s (npcs that changed storey via a stair: %d)' % (counts, len(seen)))
            unreal.unregister_slate_post_tick_callback(handle)
            editor.editor_request_end_play()
            unreal.SystemLibrary.quit_editor()
    except Exception as error:
        unreal.log_error('[STAIR_PIE] ' + str(error))
        unreal.unregister_slate_post_tick_callback(handle)
        editor.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()


handle = unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
