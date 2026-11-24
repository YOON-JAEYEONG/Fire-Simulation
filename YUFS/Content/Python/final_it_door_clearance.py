"""Measured door placement overrides for the final IT building (Unreal cm)."""
from unreal import YUFSDoorSwingSide


_SLAB_DOORS = frozenset((15, 16, 17, 18, 22, 23, 24, 25, 26))
_NORTH_21 = 21
SECOND_FLOOR_CORRECTED_DOORS = _SLAB_DOORS | {_NORTH_21}


def final_it_door_clearance(index, measured_hinge_z, width):
    """Return hinge Z, closed leaf width and swing mode; keep the observed XY."""
    if index == _NORTH_21:
        # The full-width leaf overlaps the fixed right jamb and brace. A 210 cm
        # leaf still blocks the standing capsule when closed and swings into
        # the room, out of the corridor, without altering structural collision.
        return 390.0, 210.0, YUFSDoorSwingSide.LOCAL_POSITIVE_X
    if index in _SLAB_DOORS:
        # The imported slab here spans Z=370..390, unlike other upstairs bays.
        # North_16 overlaps the opposite South_23 approach when swung into
        # the corridor; North_18 obstructs the corridor route to the stairs.
        swing = (YUFSDoorSwingSide.LOCAL_POSITIVE_X if index in (16, 18)
                 else YUFSDoorSwingSide.AWAY_FROM_OPERATOR)
        return 390.0, width, swing
    return measured_hinge_z, width, None
