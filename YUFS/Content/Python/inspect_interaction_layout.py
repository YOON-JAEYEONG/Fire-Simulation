import unreal
world=unreal.EditorLevelLibrary.get_game_world()
if not world:
    unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/Prototype')
    world=unreal.EditorLevelLibrary.get_editor_world()
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
    names=[c.get_name() for c in actor.get_components_by_class(unreal.StaticMeshComponent)]
    if 'door' in (actor.get_actor_label()+' '.join(names)).lower():
        unreal.log('[DoorLayout] %s %s %s' % (actor.get_actor_label(),actor.get_actor_transform(),actor.get_actor_bounds(False)))
# Wall scans through the known second-floor room. Returned impact points are geometry, not guessed placements.
for x in range(-650,501,50):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,-250,451),unreal.Vector(x,650,451),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
    unreal.log('[RoomScan] x=%s hit=%s' % (x,str(hit.to_tuple() if hit else None)))
