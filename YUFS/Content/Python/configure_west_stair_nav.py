"""Make the WEST staircase of Main walkable for the navmesh agent, rebuild navigation, save Main.

Navmesh sampling (Saved/stair_nav_grid.txt, 2026-10-13) showed the west stair's UPPER run (x=200..340,
y=-492..-240, z=192..350, part of the SketchUp 'Model' mesh) only got a one-cell-wide navmesh strip with a
gap at y=-320..-340, so every 2F->1F path near the west end detoured over the middle stair (~55 m). The
east stair ('계단_x1618', same step geometry 3920 cm further east) got a 3-cell strip; the difference is
voxel alignment, not the building. Fix: a hidden 4 cm 'nav ramp' box whose top surface runs through the
step noses (slope 17.5/28 = 32 deg) over the upper run. It blocks only the Vehicle channel, so the navmesh
is generated on it (navigation relevance needs Pawn or Vehicle Block) while NPC capsules, traces and the
camera ignore it: NPCs still physically walk on the real steps. Nothing of the Datasmith model changes.
Re-running replaces the ramp (label prefix YUFS_StairNavRamp_).
Run as a commandlet (spawning a StaticMeshActor under -ExecutePythonScript -NullRHI crashed the editor with
EXCEPTION_INT_DIVIDE_BY_ZERO on 2026-10-13), then bake navigation with rebuild_exit_navigation.py:
  UnrealEditor-Cmd.exe YUFS.uproject -run=pythonscript -script=<this file> -NullRHI
  UnrealEditor-Cmd.exe YUFS.uproject -NullRHI -ExecutePythonScript=.../rebuild_exit_navigation.py
"""
import math
import unreal

MAP = "/Game/Maps/Main"
PREFIX = "YUFS_StairNavRamp_"
THICK = 4.0
# name: (x0, x1, (y_bottom, z_bottom), (y_top, z_top)); the TOP surface passes through both points.
# The upper run climbs toward +Y: tread z=192 spans y=-603..-492, then 28 cm treads rising 17.5 cm each up
# to tread z=350 at y=-268..-240. The step NOSES (riser tops) lie on the line (y=-492,z=210)..(y=-268,z=350);
# that line extended down meets the z=192 tread at y=-521. A first attempt through the step heels
# ((-492,192)..(-240,350)) sat 17.5 cm inside the stair solid and produced no navmesh at all.
# 2026-10-13: a ramp on the nose line was exported (navmesh followed it below y=-394) but the cells above
# y=-394 vanished, so the run was still cut; ramps are disabled and kept only as a documented experiment.
RAMPS = {}
# Axis-aligned nav-only boxes: name -> (xmin, xmax, ymin, ymax, zmin, zmax). The east stair's top steps are
# covered by the 2F slab block (x=3960..4260, y=0..-300, z=335..350) and its navmesh steps from the z=315
# tread onto the slab. The west slab block only reaches y=-240, leaving the z=315/332 treads exposed beside
# the slab lip at x=340; Recast's ledge/steep-slope filters then drop every cell of the 140 cm run around
# y=-300..-340. This box extends the west slab block to y=-300 like the east one (navigation only, hidden).
# West_Railing: a navigation-only wall standing on the slab lip beside the top treads. Recast marks a span
# 'steep' (unwalkable) when an accessible neighbour (within AgentMaxStepHeight=35) is more than 35 above its
# other neighbours; the slab top (z=350) next to the z=315/332 treads does exactly that, and after the 2-cell
# erosion (agent radius 35 / cell 19) nothing of the west run was left at y=-324..-268. The railing turns the
# lip column into a plain wall so the surviving middle column stays connected.
BOXES = {"West_SlabFill": (40.0, 340.0, -300.0, -240.0, 335.0, 350.0),
         "West_Railing": (323.0, 345.0, -330.0, -240.0, 335.0, 500.0)}
# Navigation-only copies of another actor's mesh: name -> (source actor label, world offset). The east stair
# is its own Datasmith mesh ('계단_x1618', asset 그룹9) with the same step geometry as the west stair that is
# baked into 'Model'; a hidden, navigation-only copy placed 3920 cm west makes the west staircase present
# the same nav-collision data as the east one.
# West_StairWing: the same copy shifted a further 20 cm west, so the upper run (x=200..340) gets a 20 cm
# navigation-only 'wing' over the stairwell void (x=180..200) and keeps 3 walkable voxel columns
# (x~228..285, centred on the run) after erosion, like the east run. A 40 cm wing also worked but put the
# walkable strip on the run's open edge (NPCs descended at x~208). The copy's lower run and landing
# coincide with the real ones or lie inside the exterior wall, so they add nothing there.
MESH_COPIES = {"West_StairWing": ("계단_x1618", (-3940.0, 0.0, 0.0))}
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log("[YUFSStairNav] " + msg)


def spawn_ramp(name, x0, x1, bottom, top):
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    (yb, zb), (yt, zt) = bottom, top
    dy, dz = yt - yb, zt - zb
    length = math.hypot(dy, dz)
    angle = math.degrees(math.atan2(dz, dy))
    # Same spawn order as place_main_room_doors.spawn_box (known to work headless): location+rotation,
    # mesh, scale, label. Rotation about X tilts the local +Y axis; the sign is verified numerically.
    centre0 = unreal.Vector((x0 + x1) / 2.0, (yb + yt) / 2.0, (zb + zt) / 2.0)
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, centre0, unreal.Rotator(roll=angle, pitch=0.0, yaw=0.0))
    log("spawned %s" % actor.get_name())
    up_y = actor.get_actor_transform().transform_location(unreal.Vector(0, 100, 0))
    if up_y.z < centre0.z:
        actor.set_actor_rotation(unreal.Rotator(roll=-angle, pitch=0.0, yaw=0.0), False)
        log("flipped roll sign")
    smc = actor.static_mesh_component
    smc.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector((x1 - x0) / 100.0, length / 100.0, THICK / 100.0))
    actor.set_actor_label(PREFIX + name)
    actor.set_folder_path("YUFS/StairNav")
    log("mesh/scale/label set")
    # Centre: midpoint of the nose line moved THICK/2 down along the surface normal, so the TOP face is on the line.
    normal = actor.get_actor_transform().transform_direction(unreal.Vector(0, 0, 1))
    centre = centre0 - normal * (THICK / 2.0)
    actor.set_actor_location(centre, False, False)
    # Navigation-only collision: Vehicle Block makes it navigation-relevant, Pawn/Visibility/Camera Ignore
    # keeps NPC capsules, floor traces and the camera unaffected.
    # 'Custom' first: with a named profile (BlockAll) the responses are re-applied from the profile on load.
    smc.set_collision_profile_name("Custom")
    smc.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    smc.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    smc.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VEHICLE, unreal.CollisionResponseType.ECR_BLOCK)
    smc.set_editor_property("can_ever_affect_navigation", True)
    smc.set_visibility(False)              # not rendered in the editor either; navigation export ignores visibility
    actor.set_actor_hidden_in_game(True)
    actor.tags = ["StairNavRamp_NotPartOfBuilding"]
    log("collision set: profile=%s enabled=%s pawn=%s visibility=%s vehicle=%s navAffect=%s" % (
        smc.get_collision_profile_name(), smc.get_collision_enabled(),
        smc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN),
        smc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY),
        smc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VEHICLE),
        smc.get_editor_property("can_ever_affect_navigation")))
    top_check = actor.get_actor_transform().transform_location(unreal.Vector(0, 50, 50))     # local top-front corner
    bot_check = actor.get_actor_transform().transform_location(unreal.Vector(0, -50, 50))
    log("%s: centre=(%.1f,%.1f,%.1f) rot=%s scale=%s topSurface y=%.0f z=%.0f .. y=%.0f z=%.0f (want %s..%s)" % (
        name, centre.x, centre.y, centre.z, actor.get_actor_rotation(), actor.get_actor_scale3d(),
        bot_check.y, bot_check.z, top_check.y, top_check.z, bottom, top))
    return actor


def nav_only(actor, smc):
    smc.set_collision_profile_name("Custom")
    smc.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    smc.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    smc.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VEHICLE, unreal.CollisionResponseType.ECR_BLOCK)
    smc.set_editor_property("can_ever_affect_navigation", True)
    smc.set_visibility(False)              # not rendered in the editor either; navigation export ignores visibility
    actor.set_actor_hidden_in_game(True)
    actor.tags = ["StairNavRamp_NotPartOfBuilding"]


def spawn_box(name, x0, x1, y0, y1, z0, z1):
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0),
                                       unreal.Rotator(0.0, 0.0, 0.0))
    smc = actor.static_mesh_component
    smc.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector((x1 - x0) / 100.0, (y1 - y0) / 100.0, (z1 - z0) / 100.0))
    actor.set_actor_label(PREFIX + name)
    actor.set_folder_path("YUFS/StairNav")
    nav_only(actor, smc)
    o, e = actor.get_actor_bounds(False)
    log("%s: box (%.0f,%.0f,%.0f)..(%.0f,%.0f,%.0f) profile=%s pawn=%s" % (name, (o - e).x, (o - e).y, (o - e).z, (o + e).x, (o + e).y, (o + e).z,
        smc.get_collision_profile_name(), smc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)))
    return actor


def spawn_copy(name, source_label, offset):
    source = next((a for a in eas.get_all_level_actors() if a.get_actor_label() == source_label), None)
    if source is None:
        unreal.log_error("[YUFSStairNav] source actor %s not found" % source_label)
        return None
    src_smc = source.get_component_by_class(unreal.StaticMeshComponent)
    loc = source.get_actor_location() + unreal.Vector(*offset)
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, source.get_actor_rotation())
    smc = actor.static_mesh_component
    smc.set_static_mesh(src_smc.static_mesh)
    actor.set_actor_scale3d(source.get_actor_scale3d())
    actor.set_actor_label(PREFIX + name)
    actor.set_folder_path("YUFS/StairNav")
    nav_only(actor, smc)
    o, e = actor.get_actor_bounds(False)
    log("%s: copy of %s (%s) at %s bounds (%.0f,%.0f,%.0f)..(%.0f,%.0f,%.0f) pawn=%s" % (
        name, source_label, src_smc.static_mesh.get_name(), loc, (o - e).x, (o - e).y, (o - e).z, (o + e).x, (o + e).y, (o + e).z,
        smc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)))
    return actor


def main():
    if not les.load_level(MAP):
        log("could not load " + MAP)
        return
    removed = [a.get_actor_label() for a in eas.get_all_level_actors() if a.get_actor_label().startswith(PREFIX)]
    for a in list(eas.get_all_level_actors()):
        if a.get_actor_label().startswith(PREFIX):
            eas.destroy_actor(a)
    log("removed previous ramps: %s" % removed)
    for name, (x0, x1, bottom, top) in RAMPS.items():
        spawn_ramp(name, x0, x1, bottom, top)
    for name, (x0, x1, y0, y1, z0, z1) in BOXES.items():
        spawn_box(name, x0, x1, y0, y1, z0, z1)
    for name, (source_label, offset) in MESH_COPIES.items():
        spawn_copy(name, source_label, offset)
    saved = les.save_current_level()
    log("Main saved=%s (navmesh NOT rebuilt here: run rebuild_exit_navigation.py next)" % saved)
    if not saved:
        unreal.log_error("[YUFSStairNav] Main was NOT saved")


main()
