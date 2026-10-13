"""Configure the existing central opening only; preserve closed end-wall geometry.

This is an explicitly partial map setup, NOT a three-exit building validation.
The exterior apron is a simulation support surface, not a surveyed site model.
"""
import unreal

editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert editor.load_level('/Game/Maps/Main')
all_actors = actors.get_all_level_actors()
exits = [a for a in all_actors if a.get_class().get_name() == 'YUFSExitPoint']
central = next(a for a in exits if a.get_actor_label() == 'MainEntrance_GroundFloor')
for exit_actor in exits:
    exit_actor.set_editor_property('bPhysicalPassageEnabled', exit_actor == central)
central.set_actor_location(unreal.Vector(2155, 0, 0), False, False)
central.set_actor_rotation(unreal.Rotator(0, 0, 90), False)
for key, value in {'ExitWidth':170.0, 'PassageHeight':220.0, 'ExteriorDistance':300.0,
                   'MarkerFloorOffset':0.0, 'MarkerWallDistance':0.0,
                   'PortalFloorOffset':unreal.Vector(), 'ExteriorSpread':unreal.Vector2D(400,400),
                   'bShowDoorLeaf':False}.items():
    central.set_editor_property(key, value)

label = 'YUFS_CentralExit_SimulationApron'
pad = next((a for a in all_actors if a.get_actor_label() == label), None)
if pad is None:
    pad = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(2155,250,-10))
    pad.set_actor_label(label)
    pad.set_folder_path('YUFS/Exits')
pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
pad.static_mesh_component.set_collision_profile_name('BlockAll')
pad.set_actor_location(unreal.Vector(2155,250,-10),False,False)
pad.set_actor_scale3d(unreal.Vector(6,6,0.2))
pad.tags = ['SimulationApron_NotSurveyed']
for actor in all_actors:
    if actor.get_class().get_name() == 'NavMeshBoundsVolume':
        scale = actor.get_actor_scale3d()
        center, extent = actor.get_actor_bounds(False)
        needed = max(extent.y, 550.0-center.y)
        if extent.y > 0 and needed > extent.y:
            actor.set_actor_scale3d(unreal.Vector(scale.x,scale.y*needed/extent.y,scale.z))
assert editor.save_current_level()
unreal.log('[PHYSICAL_EXIT_SETUP] Central enabled, end markers disabled; rebuild navigation and validate in PIE.')
