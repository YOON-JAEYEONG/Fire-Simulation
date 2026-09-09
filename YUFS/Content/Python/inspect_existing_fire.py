import unreal
world = unreal.EditorLevelLibrary.get_game_world() or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
    label = actor.get_actor_label()
    cls = actor.get_class().get_name()
    if any(s in (label + cls + str(actor.tags)).lower() for s in ('fire', 'ignition', 'targetpoint', '발화')):
        unreal.log('[FireInspect] %s class=%s pos=%s tags=%s bounds=%s' % (label, cls, actor.get_actor_location(), actor.tags, actor.get_actor_bounds(False)))
        for comp in actor.get_components_by_class(unreal.SceneComponent):
            unreal.log('[FireInspect] component=%s pos=%s' % (comp.get_name(), comp.get_world_location()))
            if isinstance(comp, unreal.MeshComponent):
                unreal.log('[FireInspect] materials=%s' % comp.get_materials())
