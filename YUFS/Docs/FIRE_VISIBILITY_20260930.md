# 화재 표시 점검 — 2026-09-30

프로젝트: `C:/Users/ACSLAB4/Documents/Codex/Fire-Audit-0928/YUFS/YUFS.uproject`
브랜치: `codex/final-smoke-audit-windows`. 이번 변경도 로컬이며 커밋/푸시하지 않았다.

## 확인된 원인과 수정

현재 Windows 에디터의 효과 품질은 Low였다. UE 5.7의 `BaseScalability.ini`는 EffectsQuality 0/1에서 `r.HeterogeneousVolumes=0`을 적용한다. 이 상태의 실제 Main Play에서 FireActive, playing=true, visible=true였지만 SVT Frame은 계속 0이었다. 엔진의 `UHeterogeneousVolumeComponent::TickComponent`도 볼륨 기능 지원 확인 안에서 프레임을 증가시키므로 표시와 프레임 진행이 함께 중단된다.

`Config/DefaultScalability.ini`를 추가하여 모든 효과 품질에서 이 기능을 켰다. Low에서는 downsample factor 4 / maximum steps 64, Medium에서는 3 / 96을 사용한다. 다른 품질 설정은 그대로다. Low의 표시는 거칠 수 있다. 이것은 표시/재생 설정이며 화재의 물리 단위나 NPC 위험 임계값을 보정한 것이 아니다.

## 실제 확인

- 설정을 적용한 뒤 에디터를 완전히 종료하고 같은 프로젝트 Main을 다시 열었다. 시작 로그에서 EffectsQuality@0와 새 볼륨 설정이 적용됨을 확인했다. 재실행 시 임시 활성화 스크립트를 사용하지 않았다.
- 실제 Play의 HUD 시작 버튼을 눌렀다. 건물 위로 연기가 나타났다. 60초에서 TimelineReview로 들어갈 때 프레임은 479였다. 수정 전 같은 조건에서는 0이었다.
- 기록: `Audit0928Evidence/fire-visibility-before-quality.json`, `fire-visibility-after-quality.json`, `fire-visibility-fixed-ui.log`.
- C++ 변경은 없어서 이번 설정 수정에는 재빌드가 필요하지 않다. 이전 배치/입력 C++ 수정은 이미 빌드된 상태다.

## 불꽃 표시의 남은 확인

VDB 원본에 density/flame 그리드가 있고 선택 프레임에서 flame의 활성 복셀이 기록되어 있다. 가져온 SVT는 A=R8, B=R16F다. 임시로 연기 가림을 제거하고 불꽃 입력만 밝게 출력했을 때 전체 시점에서 불꽃 필드가 나타났다. 이는 입력 존재 확인이며 원래 재질의 불꽃 표시가 충분히 보인다는 검증은 아니다.

기존 팀 재질, 표시 밝기, 해상도 비교는 최종 수정으로 남기지 않았다. 세 MI_Fire 재질은 이번 점검 전 엔진 SparseVolumeMaterial 부모와 각 SVT 연결로 복원하여 저장했다. 임시 검사용 재질/벽 숨김/카메라 이동은 PIE 진단이며 맵에 저장하지 않았다. 원래 재질에서 불꽃을 뚜렷하게 볼 수 있는 시점, 표시 농도/밝기의 적정값은 아직 미확인이다. FireZone 카메라는 실제 UI에서 외벽을 보고 있어 불꽃 확인에 부적절했다. 좌표를 추측해 카메라/화재 위치를 저장하지 않았다.

BIN–VDB 좌표 정합 문제는 별도로 남아 있다. 시각적 연기가 재생된다고 NPC 위험 판단과 정합되었다고 말할 수 없다. 기존 감사 문서의 도메인 불일치와 데이터 변환 근거 확인이 필요하다.

## 저장과 NPC 배치

저장하지 않고 닫으면 미저장 에디터 에셋 변경만 사라진다. 현재 소스/저장된 Main/메뉴 수정이 모두 이전 버전으로 돌아간 증거는 없다. Play 중 게임 팔레트에서 배치한 NPC는 실행용이며 Play 종료 때 제거된다. 배치의 영구 저장 기능은 아직 없다.

최근 사용자 실행 로그에는 PIE 중 콘텐츠 드로어에서 BP_NPCSpawner를 여러 번 추가한 흔적과 많은 NPC 생성이 있었다. 게임 팔레트에서 개별 NPC를 놓는 흐름과 다르다. 이 흔적은 겹침/예상 밖 위치의 원인 후보이며 사용자가 말한 모든 이상 행동을 재현한 것은 아니다. NPC 배치는 메뉴 Main 진입 후 게임 팔레트 항목을 보이는 바닥으로 드래그하고 방향을 확인한 다음 HUD 시작을 누른다. 기존 배치 코드 수정과 검증 범위는 `NPC_PLACEMENT_20260930.md`에 기록되어 있다.
