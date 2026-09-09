# NPC 기능 팀 병합 계약 가이드

## 1. 목적

NPC 행동·경로 탐색·모션·상호작용을 서로 독립적으로 개발한 뒤 `zion-fs`에 병합하기 위한 코드 계약이다. 핵심 원칙은 **의사결정은 의미를 결정하고, 각 담당 모듈은 그 의미를 실행한 뒤 결과를 feedback으로 돌려주는 것**이다.

```text
Observation + perceived knowledge
  → Human Cognition
  → Intent / Behavior / Task
  → UYUFSTeamIntegrationComponent
       ├─ NavigationDirective  → 경로 담당
       ├─ MotionDirective      → 모션 담당
       └─ InteractionDirective → 상호작용 담당
  ← status / completion / blocked feedback
  → evidence revision과 재판단
```

## 2. 소유권

| 담당 | 소유 파일 | 읽어도 되지만 직접 변경을 피할 파일 |
|---|---|---|
| NPC 행동·인지 | `NPC/Cognition/*`, `NPC/Decision/YUFSHumanBehaviorSelectorComponent.*`, belief/intent 정책 | `NPC/Navigation/*`, `NPC/Animation/*`, 구체 Door/Extinguisher Actor |
| 경로 탐색 | `NPC/Navigation/*`, 신규 `YUFSTeamNavigationAdapter.*` | cognition/selector, animation, interaction 실행기 |
| 모션 | `NPC/Animation/*`, 신규 `YUFSTeamMotionAdapter.*` | belief/intent, navigation, 상호작용 예약 |
| 상호작용 | 신규 `Interaction/*`, `Fire/*`, `Door/*`, `YUFSTeamInteractionAdapter.*` | belief 계산, navigation 내부 경로, animation asset 매핑 |
| 공통 계약 | `NPC/Integration/YUFSTeamIntegrationTypes.h`, `YUFSTeamIntegrationComponent.*` | 변경 시 네 담당자 동의 및 계약 테스트 갱신 |

`AYUFSEvacuationNPC.h/.cpp`, `Core/YUFSTypes.h`, `Core/YUFSObservation.h`는 병합 충돌 가능성이 높은 파일이다. 새 기능은 우선 각 담당자의 adapter component로 추가하고, 이 파일의 수정이 꼭 필요하면 한 명이 통합 commit으로 처리한다.

## 3. 현재 제공되는 계약

### 경로 담당 입력

`FYUFSNavigationDirective` 주요 필드:

- `Revision`: 요청 식별자. feedback에 동일한 값을 반환한다.
- `Goal`: 안전 출구, 친숙 출구, 군중 목적지, 도움 대상, 조사 대상, 상호작용 대상, shelter, 재진입.
- `DestinationHint`: 현재 코어가 알고 있는 위치 힌트. 최종 경로의 정답으로 간주하지 않는다.
- `TargetStableId`: 문·소화기·물품 등 안정 식별자.
- `bAllowHazardReroute`: 위험 증가 시 재탐색 허용.
- `MaxPerceivedRisk`: NPC가 알고 있는 위험을 기준으로 한 허용 상한.

경로 담당은 도착, 차단, 실패 시 `SubmitNavigationFeedback`을 호출한다. `Blocked`/`Failed`는 NPC의 새 appraisal을 일으킨다.

```cpp
UFUNCTION()
void UYUFSTeamNavigationAdapter::HandleDirective(
    const FYUFSNavigationDirective& Directive)
{
    ActiveRevision = Directive.Revision;
    // RequestPathAsync, 위험 재평가, 이동은 이 모듈이 소유한다.
}

void UYUFSTeamNavigationAdapter::ReportBlocked(FName Reason)
{
    FYUFSTeamRequestFeedback Feedback;
    Feedback.RequestRevision = ActiveRevision;
    Feedback.Status = EYUFSTeamRequestStatus::Blocked;
    Feedback.Reason = Reason;
    TeamIntegration->SubmitNavigationFeedback(Feedback);
}
```

연결 완료 전에는 `bUseExternalNavigationDriver=false`로 둔다. 연결·도착·차단 feedback 테스트가 통과한 뒤에만 true로 바꾼다.

### 모션 담당 입력

`FYUFSMotionDirective`는 `Walk`, `Run`, `LookAround`, `Wait`, `GatherBelongings`, `Warn`, `Assist`, `Record`, `Extinguish`, `OperateDoor`, `Crawl`, `Cough`, `Freeze` 의미를 제공한다.

- 모션 담당은 어떤 행동을 할지 다시 결정하지 않는다.
- 이동 목적지와 위험 경로를 다시 계산하지 않는다.
- montage, state machine, IK, socket, VFX와 완료 notify만 담당한다.
- `LegacyAction`은 기존 11개 action/ONNX/갤러리 호환용이다.

연결 완료 전에는 `bUseExternalMotionDriver=false`로 두며 기존 `UYUFSActionAnimationComponent`가 fallback으로 동작한다.

### 상호작용 담당의 양방향 계약

상호작용 담당은 먼저 NPC가 **인지했거나 기억한 것만** `FYUFSInteractionOpportunitySnapshot`으로 제출한다.

```cpp
FYUFSInteractionOpportunitySnapshot Snapshot;
Snapshot.KnowledgeRevision = ++KnowledgeRevision;
Snapshot.bDoorActionRequired = true;
Snapshot.DoorStableId = Door->GetStableId();
Snapshot.DoorUseLocation = Door->GetUseLocationFor(NPC);
Snapshot.KnowledgeConfidence = 0.9f;
TeamIntegration->SubmitInteractionOpportunities(Snapshot);
```

의사결정 코어는 다음 요청 중 하나를 돌려준다.

- `InspectHazard`
- `OpenDoor`, `HoldDoor`, `PassDoor`
- `RetrieveBelongings`
- `AcquireExtinguisher`, `SuppressFire`
- `AssistPerson`

검색, 거리·LOS 확인, 안전 mask, 예약, 실행, 취소와 자원 해제는 상호작용 담당이 소유한다. 완료·실패·차단 시 요청 revision을 유지해 `SubmitInteractionFeedback`으로 돌려준다.

소화 행동은 다음 opportunity가 모두 참일 때만 후보가 된다.

- 교육 수준 충족
- 사용 가능한 소화기를 NPC가 알고 있음
- 초기 단계 화재를 NPC가 알고 있음
- 안전한 퇴로를 NPC가 확인함
- 즉시 생명 위험 상태가 아님

## 4. 화재 발견 전 행동

경보·연기·공식 안내가 없으면 의사결정 코어는 `ContinueRoutine`을 발행한다. 이 의미는 모션 담당이 대기, 걷기, 책상 사용 등 ambient 행동으로 표현할 수 있다. 경로 담당은 `NavigationGoal::None`일 때 기존 대피 목적지를 유지하지 않아야 한다. 상호작용 담당은 화재 전 일반 물체 행동을 추가할 수 있지만 emergency opportunity와 같은 구조에 섞지 않고 별도 태그로 구분한다.

화재 단서가 생기면 `EvidenceRevision`이 증가하고 한 번만 appraisal한다. 같은 revision에서 task choice와 commit 확률을 반복 추첨하지 않는다.

## 5. 병합 순서

1. 공통 계약과 cognition/selector를 먼저 병합한다.
2. 경로 담당 브랜치를 병합하고 `bUseExternalNavigationDriver=false`에서 directive 수신만 검증한다.
3. 경로 feedback과 fallback 비교가 통과하면 navigation driver를 전환한다.
4. 모션 담당 브랜치를 병합하고 기존 11개 animation fallback과 새 semantic mapping을 비교한다.
5. 상호작용 담당 브랜치를 병합하고 opportunity가 없을 때 소화·문 행동이 생성되지 않는지 확인한다.
6. 문·소화기·도움 대상의 reservation 경쟁 테스트를 실행한다.
7. 마지막에 `AYUFSEvacuationNPC` 통합 commit 하나에서 중복 include, tick 순서와 토글 기본값을 정리한다.

## 6. 병합 불변조건

- `EYUFSAction` 11개 순서와 `FYUFSNPCObservation::FeatureCount=28`을 조용히 변경하지 않는다.
- 새 ONNX 계약은 별도 버전으로 만든다.
- 경로 담당은 interaction Actor를 직접 소유하거나 파괴하지 않는다.
- 모션 담당은 intent·task를 변경하지 않는다.
- 상호작용 담당은 NPC가 모르는 world truth를 opportunity로 보내지 않는다.
- 오래된 revision의 feedback은 무시한다.
- 동일 자원은 동시에 한 NPC만 소유하고 문 passage capacity는 별도로 처리한다.
- 물리 불가능·확인된 치명 위험·행동불능은 utility보다 먼저 mask한다.
- 담당 모듈이 연결되지 않은 상태에서도 기존 fallback으로 프로젝트가 실행돼야 한다.

## 7. 통합 검증

- `YUFSEditor Win64 Development` 빌드 성공
- `YUFS.NPC` 자동화 전체 성공
- 같은 directive를 반복 발행해도 revision이 증가하지 않음
- 오래된 feedback이 현재 요청을 종료하지 않음
- 경로 모듈 전환 전후에 같은 목적지 의미가 유지됨
- 모션 모듈 전환 전후에 같은 high-level behavior가 표시됨
- opportunity가 없으면 소화기·문 대상 ID를 추측하지 않음
- 경로 차단·문 잠김·소화 실패가 새 appraisal로 돌아옴
- 외부 driver를 끄면 기존 경로와 애니메이션 fallback이 동작함

현재 계약 테스트는 `YUFS.NPC.Integration.TeamContracts`에 포함돼 있다.

## 8. Unreal 소화기 시연

**현재 실행 안내 (2026-09-09):** 저장소 루트의 `Launch-NaturalNpcInteractions.ps1`을 사용한다.
문 열기·주변 사람 안내·두 사람의 연속 대피는 `Launch-InteractionPreview.ps1`과
[상호작용 시각 확인 문서](INTERACTION_VISUAL_PREVIEW_KO.md)를 따른다.
기존 `-YUFSExtinguisherDemo` 자동 생성 경로는 비활성화됐다. 현재는 기존 화재 볼륨과 기존 NPC를 사용한다.

일반 대피 NPC의 자연스러운 소화기 사용을 확인하려면
`NPC_NATURAL_SUPPRESSION_KO.md`와 저장소 루트의 `Launch-NaturalNpcInteractions.ps1`을 사용한다.
이 경로는 기존 건물 NPC에 실제 상호작용을 붙인다. 문/안내 미리보기는 별도의 고정 순서 시연이므로 일반 NPC 행동 비율 검증과 구분한다.

원본 맵은 수정하지 않는다. `Prototype`은 UE 5.7 standalone 로드에서 World Partition assertion이 있으므로 일반 Editor에서 건물을 연 뒤 PIE로 실행한다. 아래 런처는 PIE를 자동 요청한다. 문/안내 미리보기는 기존 HUD의 시작 버튼을 누를 때 진행된다.

```powershell
# 저장소 루트에서 목적에 맞는 런처 하나를 실행한다.
.\Launch-NaturalNpcInteractions.ps1
# 문 열기 → 주변 사람 안내 → 두 사람 대피를 확인하려면:
# .\Launch-InteractionPreview.ps1
```

### 초기 전용 소화 시연의 구현 이력

아래는 현재 비활성화된 전용 데모의 구조 기록이다. 현재 실행/검증은 위 런처와 연결된 최신 문서를 따른다.

1. `Interaction=AcquireExtinguisher`, `Navigation=InteractionTarget` 발행
2. 소화기 단일 owner 예약과 NPC 손 부착
3. 획득 완료 feedback 후 `Interaction=SuppressFire`로 revision 갱신
4. 약제 잔량과 초기 화재 강도가 함께 감소
5. 화재 0%에서 분사 정지 및 `Completed / FireExtinguished` feedback

외형과 모션은 건물 NPC 기준으로 하나의 표시 계층을 사용한다.

- 시연 전용 Manny를 따로 생성하지 않는다. 맵에 있는
  `BP_YUFSRLEvacuationNPC`의 class, skeletal mesh, 상대 transform과 material을
  복사하며, 아직 실내 NPC가 생성되지 않은 첫 프레임에도 같은 RL NPC class와
  `/Game/NPCs/Crawling__1_` mesh를 fallback으로 사용한다.
- 이동은 기존 `Walking`, 소화기 획득·분사는 기존 two-hand animation fallback을
  사용한다. 따라서 외형과 skeleton이 대피 NPC에서 소화 NPC로 바뀌지 않는다.
- 손 본 이름은 Manny의 `hand_r`, 기존 캐릭터의 `RightHand`, Mixamo의
  `mixamorig_RightHand` 순서로 해석한다. 소화기는 선택된 손을 기준으로
  배치하고, 분사선은 소화기 `NozzlePoint`에서만 시작한다.
- 데모 중에는 NPC 위험 구체·상태 문자열을 숨기고, 소화기 내부의 중복 분사
  mesh도 숨긴다. 화면에는 interaction adapter가 계산한 분사선 하나만 표시된다.
- 모션 담당이 전용 `Extinguish` montage와 양손 IK를 납품하면 현재 two-hand
  fallback만 교체한다. 의사결정·예약·약제 소모·완료 feedback 계약은 바꾸지 않는다.

시연 구현은 `Fire/YUFSFireExtinguisher.*`, `Fire/YUFSSuppressibleFireSource.*`, `Debug/YUFSExtinguisherDemoDirector.*`에 격리돼 있다.

### 소화기 3D 에셋

시연 소화기는 OpenGameArt에 JamesWhite가 공개한 CC0 `Fire Extinguisher` 모델을
사용한다. Unreal 임포트 결과는
`/Game/Props/FireExtinguisher/OpenGameArt_JamesWhite_2015`에 있으며,
`AYUFSFireExtinguisher`가 실물 크기에 맞춰 배치한다. 하드 레퍼런스를 불러올 수
없는 개발 환경에서는 Engine 기본 도형 시각화로 자동 fallback한다.

- 원본 페이지: https://opengameart.org/content/fire-extinguisher
- 라이선스: https://creativecommons.org/publicdomain/zero/1.0/
- 프로젝트 원본/해시: `SourceAssets/FireExtinguisher/OpenGameArt_JamesWhite_2015/SOURCE.md`
- 재임포트 스크립트: `Content/Python/import_fire_extinguisher_asset.py`

원본 페이지가 CC0로 명시하므로 개인·상업 사용, 수정, 팀 저장소 포함이 가능하고
출처 표시는 의무가 아니다. 이 프로젝트는 재현성과 제작자 존중을 위해 출처를
계속 기록한다.
