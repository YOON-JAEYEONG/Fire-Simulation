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

## 2026-10-08 — new navigation development baseline

The user selected `merge_JJW_Kim_Eung_Gang` as the baseline for future navigation completion work. Remote head checked on October 8: `83cb710`. It includes our published `014f373` version and the subsequent fire-switch/grid-anchor changes (`4fc40c4`), HUD asset and Prototype external-actor changes.

Development checkout: `C:/Users/ACSLAB4/Documents/Codex/Fire-Navigation-1008/YUFS/YUFS.uproject`.
Local branch: `codex/merged-navigation-completion`. Preserve the older Fire-Audit-0928 checkout and its local training/model files. Do not directly commit/push main or the team integration branch.

This turn prepared the checkout, inspected the merged source and verified the three BIN file sizes; it did not build, run, or change gameplay. The new world-anchor mode defaults to reference fire option 0. In SVT mode the calculation still includes the selected texture frame transform and dataset BinaryGridToTexture, so the full grid transform is not necessarily invariant across options. Verify actual option/floor samples and saved map properties before assuming this establishes alignment.

Agreed completion scope, in order:
1. Verify BIN/SVT spatial and temporal correspondence across all three options and both floors; keep physical units/provenance limitations explicit.
2. Reproduce and fix repeated unsafe-route selection, distinguish unavailable data from safe samples, and provide useful no-safe-path behavior.
3. Verify doorway/stair traffic, yielding, waiting and bounded stuck recovery under crowding.
4. Connect crawl speed/sample height and exposure states consistently; account for the 60-second recording cutoff without inventing casualties.
5. Validate interaction/motion ownership with teammates and repeat representative evacuation/blockage/exposure scenarios; preserve fresh-clone reproducibility.

No new runtime result is established by this baseline review. Earlier verification records apply to their recorded commits, not automatically to this merged version.

## 2026-10-08 — Step 1 implemented and verified locally

See YUFS/Docs/NPC_NAVIGATION_STEP1_20261008.md. In merged Main, reference fire 0 was mixed with selected fire 2's calibrated SVT coordinates: BIN world Z became [-210.4,453.6], putting upstairs eyes at 504cm OutsideDomain. Per-dataset SVT calibration now takes precedence over the legacy common anchor; legacy anchor behavior is retained for non-SVT exports. NPC GetCurrentFrame now maps the live fractional playback frame instead of a stale manager Tick cursor. No raw data, map, costs, exposure, personality or timeline settings changed.

Rendered before/after 0→1→2→0 option seeks at 0/100/480/750/996: 80 raw-byte comparisons match the respective original BIN frame/cell, OutsideDomain 10→0; third domain restored to [26,666]. Final UE 5.7.4 build and seven hazard/navigation tests passed. Three rendered Main PIE runs with four temporary palette-class NPCs each, 25 seconds each, showed actual smoke sensing and evacuation-state transitions; live frame mapping mismatches zero. Automated runtime tests are not human mouse-placement verification or all-NPC escape guarantees. No diagnostics remain running; no commit/push this turn.

Original provenance/physical units/full-sequence temporal alignment remain unverified. Do not change bDatasetAlignmentConfirmed=false on the strength of this repair. Step 2 remains unimplemented: repeat UnsafePath selection was observed in live runs and needs an isolated reproduction before changing route/retry policy. User asked for one completion stage at a time.

## 2026-10-08 — Step 2 and exposure demonstration timing completed locally

See YUFS/Docs/NPC_NAVIGATION_STEP2_20261008.md and NavigationStep2Evidence. UnsafePath no longer bypasses retry limits just because the fire frame changes. Final rejected geometry receives four bounded same-floor NavMesh detour candidates; both legs/final geometry are checked against personal hazard knowledge. On refusal, store the path and hazard witnesses; periodically reassess without another query until observed clearance/new approach. Recently observed clear cells are tracked separately from positive hazard memory to avoid treating expiry as clearance. Exit selectors now honor recent failed exits, and no-alternative waiting preserves the refusal instead of clearing it. Native compatible animation uses Idle/WaitForInfo during this stationary wait.

User explicitly requested shorter exposure timing. Added SmokeExposureTimeScale=3 by default, retaining .008 base rate and .6/.9 thresholds: constant smoke=1 gives crawl25s/incap37.5s; scale1 restores75/112.5. This is demonstration time compression, not validated physiological dosing; no fabricated death probability. Existing fresh-air recovery rate unchanged. Map/raw data/60-second cutoff/ONNX inputs unchanged.

Final UE 5.7.4 build and all 76 YUFS.NPC tests passed. Three rendered Main PIE runs (4 palette-class NPCs each, 54s) verified detour selection, smaller repeated-query counts, and explicit unsafe waiting with Idle animation. Fire1/2 had4 exits each; fire3 had2 exits and2 unsafe waits. A controlled stationary real-BIN frame480 exposure run (not normal evacuation) verified scale3 crawl around26s/Crawling__1__Anim/speed80 and incap around38s/Dying/speed0; scale1 comparison still responsive at45s. Actual lowered breathing/capsule geometry is not claimed.

All diagnostics ended; no commit or push this turn. Next user-authorized completion stage is doorway/stair crowd handling, followed by crawl geometry/exposure consistency and team integration verification. Keep each stage separate as requested.

## 2026-10-11 — Step 3 traffic/recovery improvements, local only

See YUFS/Docs/NPC_NAVIGATION_STEP3_20261011.md and NavigationStep3Evidence. Same-direction followers now reduce input at a safe capsule gap instead of always stopping; traffic-held peers are not predicted as moving solely because a route remains. Exhausted recovery batches wait6s before checking current physical/hazard space, resuming only on a valid candidate. Stair candidate projection uses a slope/distance-dependent height search, followed by continuous floor and full-capsule step-up clearance checks; no actor relocation/collision bypass. Recovery timeout now accounts for distance/limited speed (2–8s). Native compatible stopped traffic animation uses WaitForInfo/Idle; external motion ownership retained. Map/fire/exposure settings unchanged.

Final build succeeded; 79 YUFS.NPC tests passed. Rendered Main before/after matched initial XYZ placements for20 ground-floor NPCs and20 upstairs NPCs. Controlled experiments used temporary early-alarm configs and paused frame0 to isolate traffic, not a change to normal population/fire behavior. All20 reached the configured exit area in both cases before and after. Ground finish sample24→26s and yielding76→111; upstairs30→28s and yielding95→87, blocked samples6→0. Do not claim a universal speedup or that permanent deadlock was reproduced in these two cases. Reopened-space-after-exhaustion was not separately enacted in a live-map experiment; backoff/physical safety/queue behavior have regression tests. Dense opposing flows/operator-blocked doors remain stress cases to test.

No gameplay assets were changed, no commit/push. All diagnostics ended. Next one-at-a-time stage: actual crawl breathing/movement geometry and exposure consistency, then timeline/team integration validation. Preserve earlier local steps and remaining source provenance limitations.


## 2026-10-15 — Step 4 physical crawl posture, local only

See YUFS/Docs/NPC_NAVIGATION_STEP4_20261015.md and NavigationStep4Evidence. Crawling now uses engine crouch collision/base/mesh handling: default half-height44cm (clamped to radius), breathing55cm above actual feet, MaxWalkSpeedCrouched limited to CrawlSpeed80. Actual low posture drives perception and path/exit/recovery height even if a ceiling refuses standing. Pose changes cancel old-filter routes/recovery and refresh sensing immediately. Ground/body heat remains checked, including intermediate heights; path scoring and rejected hazard witnesses share probe definitions. Crawl recovery hysteresis .45 avoids immediate standing into upper smoke; existing .60/.90 thresholds and .008*3 demo exposure/recovery rates retained. Timeline snapshots minimally restore posture/capsule/breathing and recorded presentation; full timeline/team integration remains the next stage.

Final Development build and all83 YUFS.NPC tests passed (4 new posture regressions). Final rendered Main PIE with real palette BP/BIN1 fixed480: stationary scale3 NPC lowered at26s and incapacitated38s because lower air was also .996 smoke; scale1 remained standing45s. Actual half88→44cm, breath152→55cm, mesh relativeZ -90→-46cm, feet preserved. Third normally moving NPC used an explicit temporary low crawl/recovery threshold .04/.02 solely for fixture coverage and moved80cm/s in Crawling animation down the real stairs to ground at39s. No normal-population escape statistics or universal crawl protection claim.

All46 runtime samples completed without Python error and editor shut down; the rendered process returned1 with RiderLink invalid-socket shutdown output, with pre-existing startup condition/default-BIN messages before option configuration. Do not report a clean engine log. No saved map/gameplay assets/data were changed, no commit/push, diagnostic processes ended. Collision is a vertical-capsule approximation, not a full prone body; existing standing-agent NavMesh does not automatically add low passages. Physical units/full-sequence alignment remain unverified and bDatasetAlignmentConfirmed=false is retained.


## 2026-10-16 — Step 5 pause/replay integration, local only

See YUFS/Docs/NPC_NAVIGATION_STEP5_20261016.md and NavigationStep5Evidence. Baseline tests reproduced missing animation pause and missing actual interaction clip/time, door and extinguisher replay. Live pause now saves/restores actual actor/movement/navigator tick and mesh pause flags, preserves movement mode/low capsule, cancels pending async requests while retaining established routes/retry waits and live interaction reservations. Repeated pause is idempotent; externally disabled clocks remain disabled.

Timeline records owned native clip/cursor/rate/loop plus door opening and extinguisher world/spray appearance/amount. Playback applies recorded poses with no live animation clock, gameplay notifies, tool ownership/consumption or door operation; fire follows the review cursor. External AnimBP mode is preserved and frozen, not graph-state-rewound. GetCurrentReviewFrameSnapshot is a read-only exact-frame diagnostic. This does not restore historical AI knowledge or support branching simulation.

Final build and89 YUFS.NPC tests passed (6 added). Rendered actual Main PIE used palette NPCs, a temporary door/tool and an explicit authored suppression gesture, not autonomous fire suppression. Runtime-only record12s/fire delay0. Two wall seconds of live pause preserved time/fire/NPC/clip/tool ownership/amount/door fraction. Seek5s exactly matched recorded clip/cursor, door fraction, tool state and fire frame; review pause2s stayed fixed; rewind/play advanced review time. Actual Stop reloaded Main to WaitingToStart/time0; a new NPC had exposure0/standing capsule/unpaused animation, tools had no owners; second Start reached FireActive/frame8. Runtime palette placements still disappear on Stop reload. No gameplay assets/maps/raw data/exposure thresholds/60-second default were changed.

All probe assertions passed and editor shut down; renderer process exit1 with existing startup Condition/default-BIN errors and RiderLink shutdown socket output, so do not call the whole engine log clean. Diagnostics ended. No commit/push. Preserve prior local steps. Remaining limitations: nearest-frame .25s replay, external graph/montage internals, recreated destroyed actors, current-result/debug HUD versus historical display, existing incapacitated hiding/exit-area policy, dense opposing crowd stress cases and unverified fire provenance/physical units (alignment-confirmed remains false).


## 2026-10-16 — Step 6 exit success geometry, local only

User cannot manually inspect the desktop and requested more careful autonomous verification. Scoped this turn to exit false positives. See YUFS/Docs/NPC_NAVIGATION_STEP6_20261016.md and NavigationStep6Evidence. Original 3 physical regression cases reproduced success through wall/closed or partially opened door, a separate nearby slab/missing floor, and NPC fixed150cm versus controller-configured radius mismatch. Ordinary350cm floors were already excluded by centre distance; do not claim otherwise.

IsAtValidExit now takes Character*, requires a real cached exit, preserves configured centre radius, checks feet height and reuses continuous floor/capsule/step clearance. The actually supported endpoint floor must match the exit floor anchor within10cm, avoiding a nearby overhead slab being used instead. Peer NPCs are ignored only by terminal-region geometry, while actual movement/recovery still collides. Queries never move the actor. NPC and controller now use the same configured radius and physical predicate. C++ call sites updated; no reflected BP signature or asset edits.

Final build succeeded;93 NPC tests passed (4 new exit cases, plus the existing origin test now has physical floor/correct body height). Rendered before/after normal Main cases each had20 ground and20 upstairs palette NPCs, same initial XYZ,40s each, all20 counted in each case. Runtime early-alarm config/fixed fire0 isolated geometry, not ordinary hazard/survival statistics. Final upright-door Main fixture: closed through12s count0, actual opening requested, full open then same stationary external-nav NPC count1 by17s. This is not autonomous door discovery.

Important evidence exclusion: the initial door probe had a Quat/Rotator script type error; positional Rotator arguments then pitched the door onto the ground. Those old/new door count1 records are NOT through-door evidence. Final fixture explicitly checks yaw=-90, upright bounds and handle reach. Original-code false success evidence comes from physical unit tests; normal before/after crowd data is valid. Final Main door is a separate after-only validation, not a fabricated matching native before comparison.

Exit remains an accessible configured region, not verified full exterior crossing. Other map anchor conventions, missing wall collision, tiny floor gaps, dense opposing crowds and unverified fire provenance remain limits. No maps/raw data/exposure/personality defaults changed, no commit/push. Final test and final door command returned0; logs retain existing startup Condition/default-BIN and RiderLink shutdown messages. Diagnostic editors ended. Verified source/DLL hashes stored in summary.json.

## 2026-10-17 — Step 7 incapacitated presentation/lifecycle, local only

User authorized continuing with the next focused stage. See YUFS/Docs/NPC_NAVIGATION_STEP7_20261017.md and NavigationStep7Evidence. Baseline 3 tests reproduced immediate hidden bodies/missed collapse selection when controller counts before NPC Tick, remaining movement/navigation clocks and hidden review poses. Incapacitated NPCs now remain visible and are counted immediately once. NotifyEpisodeFinished selects the compatible non-looping terminal native clip without overwriting CurrentAction (preserving final learning action), clears preview/interaction activity, and stops actor, navigator and movement ticks/RVO/residual input while leaving the existing skeletal clock to finish and hold its final pose. External motion ownership is preserved. Existing disabled terminal collision policy remains; no corpse obstacle/ragdoll/carrying model was added.

Actual Main review also reproduced visible terminal bodies regaining collision because ApplyTimelineSnapshot used visibility as its collision flag. Replay now keeps incapacitated/evacuated bodies non-colliding and restores healthy collision on rewind. Pause/resume retains terminal gameplay clocks disabled while pausing/restoring animation. Historical snapshot/clip/cursor/count records are checked, but final-live HUD/debug counters are not changed by this stage.

Final UE 5.7.4 build and96 YUFS.NPC tests passed (3 added). Rendered Main PIE uses2 stationary actual palette-class NPCs with real BIN1 fixed480 and existing exposure scale3 versus control1. Scale3 reaches incapacitation around38s; native Dying cursor progresses to full5.767s and holds without actor displacement. Scale1 control remains responsive through runtime-only54s recording. Two-wall-second collapse pause, exact recorded pre-collapse/collapse/start/end poses and collision, review hold, actual Stop/reload, count0/fresh exposure0/normal fresh actor clocks/collision are verified. No behavior-state forcing or saved map/asset changes. These are lifecycle fixtures, not general evacuation statistics or human visual animation QA.

Diagnostic corrections: isolated test initially omitted deferred animation evaluation; final test completes normal engine updates. First fixed-frame Main probe forgot to re-fix480 after Resume legitimately resumed fire; harness fixed, production Resume unchanged. The first completed Main run before collision repair is retained separately and must not be called final collision evidence. Final engine logs retain existing startup Condition/default-BIN and RiderLink shutdown messages; consult summary.json for exact process exits rather than inferring success from exit alone.

All changes from stages1–7 remain local on codex/merged-navigation-completion; no commit/push. A GitHub fresh clone currently does not contain these completion stages. Next stages remain smoke visibility attenuation/personal exit knowledge, dense opposing/interaction traffic stress, historical HUD presentation,3-dataset normal-run comparisons and final publication/fresh-clone verification. Keep unverified physical units/full-sequence alignment explicit and bDatasetAlignmentConfirmed=false.

Final stage7 test and Main process exits were both0; final JSON complete=true/error absent. Final recorded incap onset38.20s, clip endpoint5.767s, stationary actor, replay terminal collisionfalse versus healthytrue, actual reset count0. Diagnostic editor ended. Source and DLL hashes correspond to the final CurrentAction-preserving build.

## 2026-10-17 — Step 8 smoke visibility attenuation, local only

User authorized the next stage. Explained that personal exit memory requires unknown-exit search/guidance behavior, so this turn first scopes optical hazard sensing; global exit candidates in ChooseKnownExit/TryGetNearestKnownExit remain unchanged. See YUFS/Docs/NPC_NAVIGATION_STEP8_20261017.md and NavigationStep8Evidence.

Two baseline tests reproduced seeing600–700cm heat through100–400cm dense smoke (visual heat.797), publishing hidden cells/heat1 and refreshing their memory indefinitely. New bounded ray integration attenuates transmission by normalized smoke*distance, stops remote observations at cutoff, fades visual heat while keeping smoke itself visible. Default attenuation1.2 per normalized-density meter/cutoff.15 are heuristic cognition calibration, NOT verified physical extinction coefficients. Zero attenuation is a comparison mode. Collision walls/floors still occlude; half-world-voxel-aware spacing reduces thin-layer skipping, with5cm minimum/1024 samples maximum. Self smoke/heat, dose and local thermal probes remain independent; local thermal sampling still uses collision and may create local knowledge outside optical reach.

SampleObservedHazard now shares fog/range/horizontalFOV/geometry checks via the same pure snapshot point API, preserving observed raw values. Incomplete data cannot certify a new point observation. This does not unify general extinguisher/door/person/social discovery with fog; their existing policies remain. Diagnostics append sightCm/sightTransmission/sightDataComplete (data coverage, not alignment proof). No ONNX input dimension, exposure/personality/default timeline, map/assets/raw data changes.

Final UE5.7.4 build and102 NPC tests passed (6 new curtain/memory/thin-layer/direct-point/actual-low-eye/local-heat/missing-data cases). Rendered Main PIE: actual palette NPC pairs at identical position, fixed480 and eight headings in each of3 datasets, attenuation0 versus1.2. All24 pairs preserved self readings and never lengthened geometric sight. Largest reduction examples: data1/2 1000→158.72cm, self.9961; data3 1000→257.43cm, self.4706. These are model outputs in fixed-frame fixtures, not measured visibility; path-integrated density differs from self density. Runtime-only scale1 prevented posture change during pair comparison.

Fourth Main run used4 moving default-behavior NPCs, progressing fire1, runtime-only zero fire delay,25s. All four displaced521–885cm; detailed counts/frames in summary.json, not an all-NPC escape or survival guarantee. Final test/Main process exits both0, JSON complete=true/error absent, normal editor shutdown, existing startup Condition/default-BIN/RiderLink messages retained. Diagnostic editors ended; source/DLL hashes saved. No commit/push. Personal exit knowledge/search/guidance is next, followed by remaining crowd/HUD/3-dataset-normal-run/publication checks. Physical units/provenance/full sequence alignment still unknown; bDatasetAlignmentConfirmed=false stays.


## 2026-10-17 — Step 9 personal exit knowledge/search/guidance, local only

User authorized the next stage. See YUFS/Docs/NPC_NAVIGATION_STEP9_20261017.md and NavigationStep9Evidence. New ExitKnowledge component limits exit decisions, retreat and help guidance to personal landmarks: authored marked-entry/explicit IDs, actual wall/FOV/fog-constrained sight or nearby explicit guidance. Default Main entry prior preserves existing users; it is authored knowledge, not actual entry traversal. General alarms do not reveal exits. Prefire sight ignores preloaded future smoke; active-fire discovery requires data coverage. Unseen/unsafe destination availability uses bool so valid origin is retained. Unknown-exit search uses bounded local supported NavMesh paths, personal visits/randomness,160cm/s walking/look,2s candidate throttle,15s legs; it preserves rejected unsafe-path waiting. Search clocks pause/review correctly. Nearby moving crowd following uses actual peer feet, never its private exit destination; followers/stationary peers cannot become circular leaders. Existing explicit alert/help and staff guidance teach validated real exits; no automatic omniscience added. Team directives expose has-destination/searching flags and distinguish investigation from a safe-exit label. No ONNX dimension/default exposure/personality/timeline/map/BP/raw data changes.

Rendered Main additionally reproduced straight exit approach blocked by YUFSFireExtinguisher_1/ImportedMain: after committing around8.7s, repeated valid backwards recovery returned to the same obstacle at15/20s. Fixed-obstacle candidates now prefer physically clear180cm onward continuation, after existing NavMesh/full-capsule/floor/hazard validation. Peer yielding keeps make-room priority. No collision bypass/teleport; onward preference does not certify hazard safety. Native blockedBy diagnostics identify actor/component. Python HitResult trace helper attempts failed due unreflected/protected API; those harness errors are not final evidence.

Final UE5.7.4 build succeeded, all111 NPC tests passed (102 prior +8 personal knowledge +1 real-physics continuation). Baseline selected2 tests: familiar-action invented nearest exit failed; alarm getter already passed missing-data guard. Final rendered3-run Main JSON complete=true/error absent, process exits build/tests/Main all0: default4 upstairs movers all reached configured exit area by25s with progressing fire; unknown visitor moved and paused with state frozen, then explicit stationary co-located noncolliding guide taught Peer exit and visitor escaped by32s; unfamiliar ground visitor learned exit only after turning toward it and escaped the extinguisher approach by25s. Guide fixture tests transfer, not autonomous helping; temporary early alarm/preparation0/fixed BIN0 settings isolate search in visitor case. Do not treat4/4 as population statistics or before/after speedup. Logs retain existing Condition/default-BIN/RiderLink messages. Final source/DLL hashes in summary.json, diagnostics ended.

All1–9 stages remain local on codex/merged-navigation-completion, no commit/push; fresh GitHub clone still lacks completion stages. Bounded local exploration is not full-building completion. Review does not rewind personal knowledge or support branching. Remaining: dense opposing/interaction traffic stress, historical HUD presentation,3-dataset normal-run comparison and final publication/fresh-clone verification. Physical units/full-sequence alignment/provenance remain unknown; bDatasetAlignmentConfirmed=false stays.


## 2026-10-18 — Step10 dense opposing/held-peer/door traffic, local only

See YUFS/Docs/NPC_NAVIGATION_STEP10_20261018.md and NavigationStep10Evidence. Original global exit/memory/fire/exposure/motion ownership policies remain. New local recovery distinguishes same-direction temporary queues from stationary/interaction-held and opposing peers. Prediction could stop a mover~300cm before a stationary person, before reaching a doorway into the room where passing space existed. Stationary peers now allow reduced approach(.15–.6 input) while centre distance exceeds both capsule radii+45cm; near-body yield/collision/RVO retained. Recovery ranks already NavMesh/full-capsule/continuous-floor/personal-hazard-validated candidates by separation from the peer trajectory, physical continuation and actual passing progress. Added forward diagonals (21 bounded candidates) so sideways recovery does not immediately return to the same blockage. Checks use actual capsule centre height. Unsafe-path waits and recovery cooldown/budget remain intact. No teleport, collision bypass, forced escape, saved map/BP/raw data/default config changes.

Baseline new physical regression failed on backwards candidate selection. Final UE5.7.4 build and112 NPC tests passed (111 prior+1 expanded physical stationary/near-far approach/opposing/same-direction/forward-diagonal regression). Existing queue fixture clarified as temporarily waiting. Experimental personal stable-ID ordering worsened opposing flow and was reverted, including its experimental-policy-only test; original traffic priority/cycle/side identity stays. Do not claim bit-identical physics or a new fairness/directional reservation model. Short180cm-only/side-only intermediate candidates did not fix live Main; these are preserved separately, not final proof.

Final rendered Main3 cases complete=true/error absent; initial positions match baseline: opposing12→12 reached intended opposite targets by45s; before stationary peer's25s natural release,0/6 baseline→4/6 final had passed/reached target. Peer position remained(1500,-900,90.15), collision enabled. At25s only restore its original navigation ownership/movement ticking; no teleport/deletion. By45s7/7 including peer finish both versions. Native temporary4s door interaction opens fully/passable,6/6 by45s both versions; no forced task/TryUse from harness. Fixtures use runtime early-alarm/preparation0/vision1cm/fixed BIN1 frame0 and unsaved artificial target markers, not population statistics. Held peer is a movement-clock/external-owner fixture, actual interaction separately covered by door. Sampled~1Hz minimum active-body centres73.35cm final vs68cm summed radii, not a continuous proof.

Separate final Main with no temporary exit markers:4 upstairs default cognition/exposure NPCs, actual progressing BIN1, runtime fireDelay0/everyday disabled,25s/frame200, all4 reached the actual configured exit area. Capsule/nav cross-section scan at held-body x1500 has room laterally, but does not prove global connectivity. All final build/tests/baseline Main/final Main/default process exits0. Existing startup Condition/default-BIN/RiderLink messages remain; do not call logs clean. Source/DLL SHA256 saved. Diagnostic editors ended.

All1–10 stages still uncommitted/unpushed on codex/merged-navigation-completion; fresh GitHub clone lacks these completion stages. No main/integration commit or push. Not all dense layouts solved; directional fairness/reservation and mixed locked/hot-door/help/suppression stress remain. Next practical stages: historical review HUD values,3-dataset normal-run comparison, publication/fresh-clone verification. Physical units/full-sequence alignment/provenance still unverified; bDatasetAlignmentConfirmed=false stays.


## 2026-10-18 — Step11 historical review HUD, local only

See YUFS/Docs/NPC_NAVIGATION_STEP11_20261018.md and NavigationStep11Evidence. Baseline3 native regressions:2 fail/1 pass, showing final counts/rate/time leaking into historical/empty/new recordings. Frames now capture total participants and actual simulation elapsed time including prefire. New small GetDisplayStats reads the exact already-applied review frame, without copying all actor snapshots per HUD binding. Live getters and stored final results retain live semantics. HUD counts/rate/elapsed time use presentation stats, explicit missing-history text/time/rate, and the accurate incapacitated label. Existing BP signatures/rate0..100/nearest-frame replay remain; no saved BP/map edits. DevelopmentOnly text snapshot reads actual TextBlock/Slate bindings for audits.

Final UE5.7.4 build and115 YUFS.NPC tests pass (112 prior+3); actual result rows checked, not process exit alone. Rendered Main uses actual palette BP: one mobile scale1 exit user, stationary real-dose scale3 NPC and late stationary scale1 control added at sim12. Real BIN1 fixed480; runtime prefire2/record46. Natural exit1/incap1/total3 bysim48; historical fire0/5/15/30/40/46 display0/1/1/1/1/1 evac,0/0/0/0/1/1 incap and2/2/3/3/3/3 total, with rates0/50/33.3 and correct prefire-inclusive clocks. Actual WBP_SimHUD_C TextBlock/Slate bindings match, not screenshot layout QA. Review hold2s remains fixed; play advances; actual HUD Stop reload resets0/0/0/time0 and a fresh single-NPC recording shows0/0/1. Final stored results never change during seeks. JSON complete=true/error absent; final build/tests/Main exits0, diagnostic editor ended; source/DLL hashes saved. Stage10 movement source hashes unchanged.

Excluded probes: rendered-before script had protected Python WidgetBlueprint/WidgetTree access errors before samples, so baseline proof is native regressions, not rendered before/after. Two intermediate Main probes passed seeks/hold/play but the harness selected a detached old HUD retained by Python after reload. Binding wait alone did not fix stale selection. Confirmed engine GetAllWidgetsOfClass TopLevelOnly uses IsInViewport, then restricted final harness to currently displayed HUD and binding readiness; terminated only those diagnostic processes and reran. No product startup fix needed. Intermediate JSON is not final completion evidence. Existing startup Condition/default-BIN and RiderLink/Chromium/USB messages remain; do not call logs clean.

All1–11 stages remain local/uncommitted/unpushed on codex/merged-navigation-completion; fresh GitHub clone does not include them. No main/integration commit. Remaining:3-dataset normal progressing comparisons, mixed locked/hot-door/help/suppression crowd stress and publication/fresh-clone verification. No physical unit/provenance/full-sequence alignment proof; bDatasetAlignmentConfirmed=false. Review does not restore historical cognition or support branching.


## 2026-10-18 — Combined remaining checks and authorized JJW_NPC_BEHAVIOR publication

User explicitly authorized grouping remaining checks and publishing all completion work to JJW_NPC_BEHAVIOR. Main/integration branches must remain untouched. Remote JJW_NPC_BEHAVIOR was an ancestor of83cb710 base; preserve integration and stages1–11. See NPC_NAVIGATION_RELEASE_20261018.md and NavigationReleaseEvidence. Three real Main FireOptions progressed normally to60s/frame479 with20 identical placements/stable IDs/default cognition and exposure (runtime delay0/everyday off): evac/incap/unresolved=19/0/1,17/3/0,11/2/7. Never force outcomes. Unresolved live samples show UnsafePath waits and low-posture attempts; final-review component resets must not be mistaken for historical perception. Physical data provenance/units/full alignment still unverified.

Four rendered mixed Main fixtures45s each: locked/hot doors remain closed/impassable while6/6 take available side space; real rule-fallback help/reservation/contact/guidance completes and native slow door opens,7/7; authored15s visual spray releases tool/presentation and native slow door opens,7/7. Temporary targets/props/early-alarm/fixed BIN0 isolate traffic, not population statistics. Help recipient movement owner returned only after native need cleared; no forced success/teleport/body collision disable. Spray is authored presentation, not autonomous FDS sensing/suppression; metadata/alignment gates remain false, recorded fire and agent amount unchanged. All probes complete/error absent/process0. Full YUFS119 tests pass including115 NPC tests; do not claim clean startup/shutdown logs.

Fixed both safe CMD launchers reusing global C:\CodexWork junction without verifying target: now always operate on own checkout, never delete shared junction. New read-only Tools/Test-YUFSCheckout.ps1 verifies branch/commit,3 BIN SHA256/sizes, map/SVT package headers; quick launch checks local DLL/engine BuildId/source age. Windows PowerShell .NET hashes avoid nested-shell Get-FileHash autoload failure. CMD --verify-only and correct-build/wrong-branch/wrong-commit checks passed. Stage11 C++/DLL hashes unchanged; README updated. Publishing/fresh remote clone verification follows; do not claim publication until exact remote SHA is verified. Never copy old DLL into clone to pass.
