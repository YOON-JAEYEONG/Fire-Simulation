import unreal
world = unreal.EditorLevelLibrary.get_game_world()
pc = unreal.GameplayStatics.get_player_controller(world, 0)
size = pc.get_viewport_size()
ray = pc.deproject_screen_position_to_world(size[0] * 0.342, size[1] * 0.288)
unreal.log('[MarkedRoom] viewport=%s ray=%s' % (size, ray))
origin, direction = ray
hit = unreal.SystemLibrary.line_trace_single(world, origin, origin + direction * 30000,
    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
unreal.log('[MarkedRoom] hit=%s' % str(hit))
unreal.log('[MarkedRoom] details=%s' % str(hit.to_tuple()))
for z in (5, 355):
    point = origin + direction * ((z - origin.z) / direction.z)
    unreal.log('[MarkedRoom] floorZ=%s candidate=%s' % (z, point))
