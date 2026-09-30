# Windows NPC / fire audit — 2026-09-28

## Checkout and boundaries

- Base: `Final_merge_2@df6017163db289731db29f495d89337015799907`; remote fetch on September 28 matched the Mac audit.
- Work branch: `codex/final-smoke-audit-windows`.
- Project: `C:/Users/ACSLAB4/Documents/Codex/Fire-Audit-0928/YUFS/YUFS.uproject`.
- Desktop's original `Fire-Simulation` is a different checkout (`JJW_CYW_ZION`); it was not modified.
- Changes are local, uncommitted and unpushed. Do not commit/push `Final_merge_2` or main.

## Verified inputs

External data: `C:/Users/ACSLAB4/Desktop/it_test_vdb&bin데이터`.
All three BIN SHA-256 hashes match the Mac `data-audit.json` exactly. BIN 1/2: 997 frames; BIN 3_v1: 998 frames. All grids 116x53x20. Each VDB ZIP contains 997 files.
BIN files were copied into the isolated project's `Content/Fires/FirePrototype/BinaryData` under their original names. Raw BIN/VDB files remain ignored/external dependencies.

## Confirmed changes

1. Regression test reproduced false evacuation with no exit and after exit destruction. `IsAtValidExit` now requires a still-valid cached exit for both NPC terminal status and controller counting. World-origin exits remain valid. Existing distance thresholds are unchanged; this does NOT solve through-wall/radius geometry concerns or establish that the team's Main run triggered the bug.
2. `GetHazardAudit()` provides on-demand NPC world/eye position, current BIN frame, continuous cell coordinates, flat index, raw byte channels, sample validity, cached perception frame, Self/Front/Above, path maxima, navigation failure, behavior state, exposure. Current eye sampling and cached perception are explicitly separate.
3. Navigation log and debug overlay labels now explicitly say path maximum smoke/heat. No hazard thresholds, exposure rates, personalities or route costs were changed.
4. Main's three FireOption materials were empty in the fresh checkout. Imported the supplied VDB sequences (997 frames each) into `Content/Fires/Audit0928` and linked material instances using the engine SparseVolumeMaterial parent. Corrected option 3's filename to `smoke_data_it_test_3_v1.bin`. Kept map transforms, playback settings and NPC placement unchanged. These are data-connection preparations, not proven spatial alignment.
5. Added narrow `.gitignore` exceptions for these six SVT/material `.uasset` dependencies so a later commit cannot silently omit them. No commit has been made.

## Verification so far

- UE 5.7 Windows baseline and changed-source builds succeeded. MSVC 14.51 reports a non-preferred-toolchain warning.
- Exit regression failed on baseline with the intended no-exit/destroyed-exit assertions, then passed after the change.
- NPC suite: 71 passed, 0 failed (5 tests with warnings). Subsequent label/unused-variable cleanup and bounds diagnostics were successfully rebuilt (`final-final-build.log`, `final-bounds-build.log`). These last edits affect diagnostics, not movement decisions.
- Original Main editor inventory AND ordinary rendered PIE: no NPC, no ExitPoint, no LevelDataManager. Runtime GameMode is GameModeBase, with default PlayerController/Pawn. User was asked for the team's exact fire option and saved NPC/exit placement; no answer yet. Do not invent their setup or claim the stair failure was reproduced.
- Ordinary rendered Main Play: BIN and VDB advanced, then simulation entered TimelineReview at sim time 60.06 seconds and stayed there. 75 wall seconds captured. No NPCs were added. This confirms the actual saved Main's 60-second stopping behavior, not behavior of an unprovided menu/scenario override.
- First rendered Play failed due sandbox shader working-directory access. Isolated-cache retry was stopped to reuse the existing Windows editor cache. Escalated default-cache rendered runs completed successfully and exited. No audit editor is intentionally left running.

## Alignment evidence and remaining work

Imported SVT transforms include scale/sign and origin offsets absent from the BIN GridToWorld formula. Fire 1/2 SVT transform has rotation Y=180deg, scale=(-0.4,0.4,0.4), translation=(4.8,-0.8,-2.0). Fire 3 translation=(2.8,-2.8,-0.8) with same rotation/scale. Main volume transforms also contain rotation X=180deg and nonuniform scale. BIN VoxelSize=40cm, GridOriginLocal=0, frame ratio=1, offset=0, alignment unverified. Need compare actual voxel indices and raw channel values, not just infer alignment from matching frame counts. Do not enable alignment confirmation or guess an axis flip/offset without export evidence.

Rendered PIE switching all three options at frame zero confirmed Ready BIN and matching intended asset references. World Z bounds (cm):

| Option | BIN domain | Rendered SVT bounds |
| --- | --- | --- |
| 1 | -1001.667 to -310 | -144 to 719.2 |
| 2 | -1001.667 to -310 | -144 to 719.2 |
| 3 | -710 to -70 | -6 to 730 |

All three domain bounds are vertically disjoint in this setup. This confirms a spatial discrepancy in the reconstructed fresh-checkout configuration. Bounds are not occupied-smoke measurements, and this is not a reproduction of the team's particular stair NPC. No automatic axis flip/scale correction was applied: BIN export index conventions and VDB processing must be established first. Ordinary playback diagnostics also showed the cached BIN frame can trail the visual frame within a tick (e.g. 26 vs 29); this is separate from dataset temporal provenance and not proof of a fixed file offset.

Still needed: actual stair NPC stop recording, team-equivalent placement/fire selection, physical units and conversion scripts, frame timing provenance, runtime 60-vs-180-second comparison with the actual NPC placement. Do not change exposure thresholds to manufacture casualties. No conclusion on why the team's 20/20 escaped or stair NPC stopped yet.

## Evidence and scripts

All are under `C:/Users/ACSLAB4/Documents/Codex/2026-09-07/https-github-com-yoon-jaeyeong-fire-2/work/`:

- `final_map_inventory.json`, `final-map-inventory.log`: original saved map inventory.
- `final_setup_data.json`: imported frames/transforms and saved Main linkage.
- `final-exit-before/index.json`: failing regression on baseline.
- `final-tests-after/index.json`: 71 passing tests.
- `final-fix-build.log`: successful changed-source build.
- `final_main_play.py`, `final_main_play.json`, `final-main-play-cached.log`: completed ordinary EditorRequestBeginPlay with renderer, no actor placement overrides, 75s wall-clock capture.
- `final_alignment_probe.py`, `final_alignment_probe.json`, `final-alignment-probe.log`: completed rendered PIE, selects options 0/1/2 before starting and compares BIN and rendered bounds. No transform overrides.
- `final_import_vdb.py`, `final_setup_data.py`: local import/setup scripts. Paths reference extracted data in `work/final-vdb`. Setup has been iterated; check idempotency before re-running against existing assets.

Unreal automation tests/NullRHI validate code behavior, not visible smoke. The rendered run is offscreen automation; it is not a human manual-play observation.

## 2026-09-29 Main launch wiring repair

Main now includes a PlayerStart at the existing overview camera transform, OverviewCamera/FireZoneCamera tags on existing cameras, and an explicit modern HUD class. Added one YUFSLevelDataManager and one YUFSEmergencyCommSystem. Added MainEntrance at (2150,-20,0), width 180 cm: ground-floor wall mesh section at z=100 confirms an exterior opening from x=2060 to 2240 at y=0. This is a new Main configuration, not a recovered team experiment placement. Prototype coordinates were not copied.

The map still uses manual NPC palette placement. Do not add automatic sample NPCs. Test NPCs are spawned only in PIE and never saved. Default scenario fire delay remains 30 seconds. Fixed camera buttons need PLAYER mode to return to the movable pawn. No fire alignment, exposure or behavior thresholds were changed in this repair.

Scripts: work/repair_main_wiring.py, repair_main_exit.py, verify_main_wiring.py in the original chat workspace. The OBJ/BMP floor section is geometry evidence, not a game screenshot. Offscreen screenshot capture returned black, so no claim of visually verified on-screen UI or literal mouse/keyboard testing is justified.

Verification completed: 75-second rendered PIE through LaunchScenario from Lvl_MainMenu. All three camera handlers resolve correct view targets; movement input displaces the default pawn. Three temporary actors of the palette-selected BP_YUFSRLEvacuationNPC class moved and all three logged evacuation success, including a second-floor staircase route. Paths from all three sampled locations are valid/non-partial. Checks and detailed samples are copied into Docs/Audit0928Evidence/verify_main_wiring*.json. This invokes native HUD handlers and runtime spawning, not physical mouse drag/drop or keyboard events. Samples remain OutsideDomain/zero smoke, so this is wiring/path movement verification only. No new source change or rebuild was needed for the map repair; earlier audit build remains in use. Changes remain local/uncommitted.

## Actual menu UI reproduction, 2026-09-29 afternoon

The earlier native LaunchScenario test explicitly selected Main and missed an asset bug. Using Windows UI Play -> New Simulation -> Start loaded /Engine/Maps/Templates/Template_Default. WBP_ScenarioSetup.AvailableMaps[0] was labeled Prototype but referenced the engine blank template. This route has no simulation controller/HUD to restore game input after UIOnly menu mode. Replaced this option with /Game/Maps/Main.Main, label Main (화재 시뮬레이션); retained the second Prototype option. Save verified from the CDO after explicitly assigning the modified struct back into the array. Blueprint asset now modified.

Additional focus restoration: SimulationController::SpawnHUD sets GameAndUI and viewport focus AFTER widget creation. SimHUD Start/Player buttons restore GameAndUI/focus. Built successfully in input-focus-build.log. Windows UI verification is underway; do not equate earlier direct AddMovementInput checks with hardware-event input verification. Evidence before repair is work/manual-input-before.json and .log; repair_menu_map.json records exact references.

Verification finished: actual Windows UI Play -> New Simulation -> Start now loads Main and shows the simulation HUD. Mouse drag changed control rotation from pitch=-37.086/yaw=92.269 to pitch=-51.259/yaw=118.691. Short injected W taps did not provide reliable movement evidence; the user then explicitly tested holding W and confirmed movement (W로 이동함). No forced pawn displacement was used during this UI verification. Leave the editor open for the user. Build succeeded; all changes remain local, no commit/push. The remaining BIN/SVT alignment issue is not solved by this menu/input fix.

## September 30 — placement correction

See `YUFS/Docs/NPC_PLACEMENT_20260930.md`. Replaced guessed NavMesh-floor picking with cursor surface trace, same-floor navigation validation and capsule clearance. Rejects unsafe drops instead of relocating spawn; holds pending NPC during rotation confirmation; cleans widget timers/temporary actors. Main PlayerStart is now nearer the building. Actual menu/palette drag, confirmation, cancellation and sky rejection were exercised. These were upper-floor drops; all-floor collision coverage and the existing BIN/VDB alignment remain unverified. Source rebuilt successfully. No commit or push.

## September 30 — Low effects quality disabled fire playback

See `YUFS/Docs/FIRE_VISIBILITY_20260930.md`. Confirmed actual Main Play FireActive/playing/visible but SVT frame stuck at 0 with `r.HeterogeneousVolumes=0`, set by UE's Low effects preset. Added DefaultScalability.ini enabling volumes at every effects quality with lower rendering cost at Low/Medium. After a full editor restart at Low, actual Play showed smoke and advanced to frame 479 by the 60-second TimelineReview. This changes configuration only; no C++ rebuild was required this turn. No commit/push.

Flame input was visible with a temporary isolated diagnostic material; original-material flame appearance is NOT yet verified. Team-material/brightness experiments were restored to the prior engine-parent MI_Fire1/2/3 configuration and saved. No diagnostic camera or hidden geometry was saved to Main. FireZone camera still looks at an exterior wall. BIN/SVT spatial correspondence remains unverified. Recent user logs show multiple BP_NPCSpawner content-drawer drops into PIE; distinguish these from game-palette placement. PIE placement is not persisted to the editor map.

## September 30 evening — NPC sensing coordinates and alarm coverage

See `YUFS/Docs/NPC_HAZARD_RESPONSE_20260930.md`. Reproduced actual NPC OutsideDomain/zero sensing because the legacy field omitted the SVT per-frame transform and used incompatible grid origin/spacing. Added opt-in per-FireOption BIN-to-SVT index mapping, composed with the current SVT frame transform and component transform; option changes refresh spatial and temporal settings together. Old map defaults remain legacy. Main's three datasets have empirical grid offsets (7,-3,4)/(7,-3,4)/(2,3,1), derived by decoding selected VDB frames and correlating BIN fields. About one voxel of spatial uncertainty remains; late VDB frames in datasets 1/2 disagree with BIN. `bDatasetAlignmentConfirmed=false` remains intentional: do not present this as verified FDS units/provenance or enable suppression evidence from it.

Main communication actor moved to building center (2150,-900,350), retaining 3000cm radius/personality response variation. Diagnostics now include NPC alarm/Tick/activity and mapping/frame parameters. Development Win64 build and five hazard/navigation tests passed. Main PIE with six palette-class NPCs showed direct Smoke/Heat transitions and four exit successes over 58 seconds, with UnsafePath retry/stuck cases still remaining. Actual Windows game-palette drag/rotation-confirm/HUD Start also worked: a sceptical NPC initially waited after alarm, then committed on Smoke and reached a valid exit around 19 seconds. This is not an all-NPC or full-sequence accuracy guarantee. Source/map saved locally; no commit/push.

## Publication to JJW_NPC_BEHAVIOR (2026-09-30)

User authorized publishing the tested Windows version to JJW_NPC_BEHAVIOR. This includes Main/menu/assets/config/source and the three Main BIN files via Git LFS. Follow root README.md for fresh-clone setup; previous external-data/local-only statements above are historical. Do not publish generated DLLs, editor caches, or temporary training runs. Main and Final_merge_2 are not push targets. Exact remaining sensing/navigation limitations are in NPC_HAZARD_RESPONSE_20260930.md.
