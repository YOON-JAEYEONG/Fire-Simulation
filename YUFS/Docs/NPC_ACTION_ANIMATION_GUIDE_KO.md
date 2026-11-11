# NPC 행동 애니메이션 확인 가이드

## 확인 방법

1. `Launch-YUFS-Safe.cmd`로 프로젝트를 연다.
2. 메인 맵에서 Play(PIE)를 누른다.
3. 분산 배치가 끝나면 카메라가 실내 대표 NPC 앞으로 자동 이동한다.
4. 화면 위 검수 패널의 `Action`과 `Animation`을 확인한다. 11개 행동이 4초 간격으로 자동 순환한다.
5. 검수 중에는 다른 NPC와 위험도 디버그 도형을 잠시 숨기고 카메라 조명을 켜므로 관절 동작을 가리지 않는다.
6. HUD의 시뮬레이션 시작 버튼을 누르면 원래 카메라와 모든 NPC가 복원되고 실제 AI 행동에 맞춰 애니메이션이 전환된다.

`SimulationController > Simulation > NPC Animation Preview > Preview All NPC Action Animations`를 끄면 전시 모드를 비활성화할 수 있다.

세부 검수 설정은 같은 카테고리의 `Auto Focus NPC Action Animation Showcase`, `Isolate Focused NPC In Animation Showcase`, `NPC Action Preview Seconds Per Action`에서 조정한다.

## 행동별 매핑

| 행동 | 적용 애니메이션 | 재생 방식 | 화면에서 보이는 의미 |
|---|---|---:|---|
| `Idle` | `Idle` | 반복, 1.00x | 일상 대기 |
| `SeekInformation` | `Idle` | 반복, 0.82x | 느린 대기 포즈 + 실제 행동 중 좌우 탐색 회전 |
| `AlertNearbyOccupants` | `Talking` | 반복, 1.12x | 주변 사람에게 알림 |
| `GatherBelongings` | `Standing_Using_Touchscreen_Tablet` | 반복, 0.72x | 손 주변을 확인하며 물품을 챙기는 대체 동작 |
| `EvacuateToNearestExit` | `Fast_Run` | 반복, 1.00x | 가장 가까운 출구로 대피 |
| `EvacuateToFamiliarExit` | `Fast_Run` | 반복, 0.92x | 익숙한 경로로 대피 |
| `HelpOther` | `Walking` | 반복, 0.85x | 도움 대상과 속도를 맞춘 이동 |
| `WaitForInfo` | `Idle` | 반복, 0.58x | 공식 안내 대기 |
| `Cough` | `Standing_Cough_Combined_1` | 반복, 1.00x | 연기 흡입 기침 |
| `FollowCrowd` | `Fast_Run` | 반복, 1.08x | 군중을 빠르게 추종 |
| `Film` | `Standing_Using_Touchscreen_Tablet` | 반복, 1.00x | 휴대기기 촬영 |

행동 외 생리 상태도 우선순위 애니메이션을 갖는다.

| 상태 | 적용 애니메이션 | 우선순위 |
|---|---|---|
| `Crawling` | `Crawling__1__Anim` | 현재 행동보다 우선 |
| `Incapacitated` | `Dying` | 최우선, 1회 재생 |

## 구현 구조

`UYUFSActionAnimationComponent`가 행동과 애니메이션 에셋의 매핑을 소유한다. NPC는 행동 또는 생리 상태가 바뀔 때만 컴포넌트에 갱신을 요청한다.

- 에셋은 soft reference로 보관하고 최초 사용 시 로드한다.
- 현재 Skeletal Mesh와 애니메이션 Skeleton이 다르면 재생하지 않고 오류 로그를 남긴다. 현재 맵의 `Crawling__1__Skeleton`과 같은 원본 `/Game/NPCs` 시퀀스를 사용한다.
- 반복 애니메이션은 stable NPC ID로 시작 위치를 분산해 모든 NPC의 팔·다리가 동시에 움직이는 현상을 줄인다.
- 미리보기 행동은 실제 `CurrentAction`을 바꾸지 않아 정책·학습 로그에 들어가지 않는다.
- 시뮬레이션 시작 시 전시 모드를 해제해 AI 행동 애니메이션으로 복귀한다.

## 현재 에셋 범위에 따른 주의점

프로젝트에 전용 물품 회수, 안내 대기, 부축 모션이 아직 없으므로 같은 동작 계열의 리타깃 에셋과 다른 재생 속도를 사용했다. 전용 모션을 추가할 때는 NPC의 `YUFSActionAnimationComponent > Action Animations` 항목에서 에셋만 교체하면 의사결정 코드는 수정하지 않아도 된다.
