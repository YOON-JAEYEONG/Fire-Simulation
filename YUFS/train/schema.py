from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


@dataclass(frozen=True)
class ObservationField:
    index: int
    name: str
    description: str


OBSERVATION_FIELDS = [
    ObservationField(0, "smoke_density_at_self", "Normalized smoke density at the NPC position."),
    ObservationField(1, "temperature_at_self", "Normalized temperature at the NPC position."),
    ObservationField(2, "smoke_in_front_normalized", "Maximum normalized smoke level seen in front of the NPC."),
    ObservationField(3, "smoke_above_normalized", "Maximum normalized smoke level seen above the NPC."),
    ObservationField(4, "risk_level", "Perception component risk estimate in [0, 1]."),
    ObservationField(5, "sim_time_normalized", "Simulation time normalized by the fire frame range."),
    ObservationField(6, "dist_to_nearest_exit_normalized", "Distance to nearest safe exit normalized by 10000 cm."),
    ObservationField(7, "dist_to_familiar_exit_normalized", "Distance to familiar exit normalized by 10000 cm."),
    ObservationField(8, "dir_to_nearest_exit_x", "X component of the unit direction vector to nearest safe exit."),
    ObservationField(9, "dir_to_nearest_exit_y", "Y component of the unit direction vector to nearest safe exit."),
    ObservationField(10, "dir_to_nearest_exit_z", "Z component of the unit direction vector to nearest safe exit."),
    ObservationField(11, "nearest_exit_smoke_free", "1 when nearest safe exit location is not dangerous, else 0."),
    ObservationField(12, "nearby_evacuating_ratio", "Fraction of nearby NPCs currently evacuating."),
    ObservationField(13, "nearby_npc_count_normalized", "Nearby NPC count normalized by 20."),
    ObservationField(14, "group_size_normalized", "Nearby group size including self, normalized by 20."),
    ObservationField(15, "nearby_npc_needs_help", "1 when nearby social context suggests helping, else 0."),
    ObservationField(16, "alarm_sounding", "1 when alarm has reached the NPC, else 0."),
    ObservationField(17, "received_pre_recorded_message", "1 when prerecorded emergency message has been received."),
    ObservationField(18, "received_live_announcement", "1 when live emergency announcement has been received."),
    ObservationField(19, "received_staff_guidance", "1 when staff guidance has been received."),
    ObservationField(20, "staff_guided_exit_x_normalized", "Normalized X coordinate of staff-guided exit target."),
    ObservationField(21, "staff_guided_exit_y_normalized", "Normalized Y coordinate of staff-guided exit target."),
    ObservationField(22, "staff_guided_exit_z_normalized", "Normalized Z coordinate of staff-guided exit target."),
    ObservationField(23, "current_state_normalized", "Behavior state enum normalized by the max enum value."),
    ObservationField(24, "risk_perception", "State machine risk perception in [0, 1]."),
    ObservationField(25, "stress_level", "Current stress proxy copied from perception risk level."),
    ObservationField(26, "milling_action_count_normalized", "Milling action count normalized by 20."),
    ObservationField(27, "smoke_exposure_accumulated", "Accumulated smoke exposure in [0, 1]."),
]


OBSERVATION_DIM = len(OBSERVATION_FIELDS)


class ActionId(IntEnum):
    Idle = 0
    SeekInformation = 1
    AlertNearbyOccupants = 2
    GatherBelongings = 3
    EvacuateToNearestExit = 4
    EvacuateToFamiliarExit = 5
    HelpOther = 6
    WaitForInfo = 7
    Cough = 8
    FollowCrowd = 9
    Film = 10


class TerminalReasonId(IntEnum):
    None_ = 0
    ReachedExit = 1
    Incapacitated = 2
    TimedOut = 3


ACTION_NAME_TO_ID = {action.name: int(action) for action in ActionId}
ACTION_ID_TO_NAME = {int(action): action.name for action in ActionId}

TERMINAL_NAME_TO_ID = {
    "None": int(TerminalReasonId.None_),
    "ReachedExit": int(TerminalReasonId.ReachedExit),
    "Incapacitated": int(TerminalReasonId.Incapacitated),
    "TimedOut": int(TerminalReasonId.TimedOut),
}
TERMINAL_ID_TO_NAME = {value: key for key, value in TERMINAL_NAME_TO_ID.items()}
