"""Install readable 2F wayfinding signs linked to the three actual exits."""

import unreal


MAP = '/Game/Maps/it관_찐최종/Main_level'
TAG = 'YUFSFinalITStairSign'
# Measured on the upper corridor's navigable strip. Faces cover both approaches.
# These coordinates are sign locations, never movement or evacuation endpoints.
SIGN_SPOTS = {
    'West': [2950, 3940, 5000],
    'Main': [4650, 5700, 6360, 7300],
    'East': [7010, 8130, 9200],
}


def install():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert not level.is_in_play_in_editor(), 'Stop PIE before editing Main_level'
    assert level.load_level(MAP), MAP
    all_actors = actors.get_all_level_actors()
    exit_class = unreal.load_class(None, '/Script/YUFS.YUFSExitPoint')
    exits = {str(a.get_editor_property('exit_id')): a for a in all_actors
             if a.get_class() == exit_class}
    for direction in SIGN_SPOTS:
        assert 'Final_' + direction in exits, 'Missing actual exit '+direction
    for a in all_actors:
        if isinstance(a, unreal.TextRenderActor) and TAG in [str(t) for t in a.tags]:
            actors.destroy_actor(a)
    for direction, spots in SIGN_SPOTS.items():
        markers = []
        for i, x in enumerate(spots, 1):
            for face in (-1, 1):
                location = unreal.Vector(x + face*2, -8680, 565 if x >= 5750 else 585)
                sign = actors.spawn_actor_from_class(unreal.TextRenderActor, location,
                    unreal.Rotator(0, 0, 0 if face > 0 else 180))
                sign.set_actor_label(f'YUFS_2F_StairSign_{direction}_{i}_{face}')
                sign.set_folder_path('MergeFinal/Wayfinding2F')
                sign.tags = [TAG]
                sign.set_actor_enable_collision(False)
                text = sign.text_render
                text.set_text(direction.upper() + ' STAIRS')
                text.set_world_size(24)
                text.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
                text.set_text_render_color(unreal.Color(145, 245, 185, 255))
                markers.append(sign)
        exits['Final_' + direction].set_editor_property('wayfinding_markers', markers)
    assert level.save_current_level(), 'Failed to save Main_level wayfinding'
    unreal.log('[YUFSStairWayfinding] Three registered exits have 20 two-sided wayfinding faces')


if __name__ == '__main__':
    install()
