"""Editor-only (needs ticks): rebuild the Main navmesh, wait for the async tile build to really finish,
sample the navmesh over the three staircases and run 2F->1F path checks in the editor world, then save Main.
Writes Saved/stair_nav_after_bake.txt. Run: UnrealEditor-Cmd.exe YUFS.uproject -NullRHI -ExecutePythonScript=<this>
"""
import os, time, unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUT = os.path.join(unreal.Paths.project_saved_dir(), "stair_nav_after_bake.txt")
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level('/Game/Maps/Main')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
state = {"start": time.monotonic(), "seen_building": False, "quiet_since": None, "ticks": 0, "done": False}
L = []
def log(m): unreal.log("[NAV_BAKE] " + m); L.append(m)
def nav_hits(x, y):
    hits = []
    for zc in (30, 90, 150, 210, 270, 330, 390):
        r = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(x, y, zc), None, None, unreal.Vector(4, 4, 32))
        if isinstance(r, tuple): r = r[0]
        if r is not None and not (r.x == 0 and r.y == 0 and r.z == 0):
            z = int(round(r.z))
            if not hits or abs(hits[-1] - z) > 4: hits.append(z)
    return hits
def sample():
    for name, dx in (("WEST", 0), ("EAST", 3920)):
        L.append("# %s stair upper run: nav heights, rows y, cols x" % name)
        L.append("      " + " ".join("%4d" % (x + dx) for x in range(210, 340, 20)))
        for y in range(-220, -560, -20):
            L.append("%5d " % y + " ".join("%4s" % ("/".join(str(v) for v in nav_hits(x + dx, y)) or ".") for x in range(210, 340, 20)))
    for name, a, b in (("West 2F top -> 1F foot", (270, -200, 355), (110, -76, 5)), ("West 2F hall -> WestEnd target", (520, -400, 355), (-300, -920, 0)),
                       ("East 2F top -> 1F foot", (4190, -200, 355), (4030, -76, 5))):
        p = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, unreal.Vector(*a), unreal.Vector(*b), None, None)
        pts = list(p.path_points) if p else []
        length = sum(unreal.Vector.distance(pts[i], pts[i+1]) for i in range(len(pts)-1))
        west = any(40 <= q.x <= 400 and -660 <= q.y <= -40 and 30 <= q.z <= 320 for q in pts)
        log("%s: valid=%s partial=%s points=%d length=%.0f passesWestStair=%s" % (name, p.is_valid() if p else None, p.is_partial() if p else None, len(pts), length, west))
def tick(dt):
    if state["done"]: return
    state["ticks"] += 1
    now = time.monotonic()
    building = unreal.NavigationSystemV1.is_navigation_being_built(world)
    if building:
        state["seen_building"] = True; state["quiet_since"] = None
    elif state["quiet_since"] is None:
        state["quiet_since"] = now
    if state["ticks"] % 60 == 0:
        log("t=%.1fs building=%s seenBuilding=%s" % (now - state["start"], building, state["seen_building"]))
    ready = (state["seen_building"] or now - state["start"] > 20) and state["quiet_since"] is not None and now - state["quiet_since"] > 3
    if not ready: return
    state["done"] = True
    unreal.unregister_slate_post_tick_callback(handle)
    log("navmesh build finished at t=%.1fs (seenBuilding=%s)" % (now - state["start"], state["seen_building"]))
    sample()
    saved = editor.save_current_level()
    log("Main saved=%s" % saved)
    open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
    unreal.SystemLibrary.quit_editor()
unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
handle = unreal.register_slate_post_tick_callback(tick)
