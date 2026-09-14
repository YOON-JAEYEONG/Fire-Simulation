# 화재 대피 NPC 행동·환경 상호작용 구현 기획안

> 문서 성격: `zion-fs`의 현재 구현을 출발점으로 삼되, 아직 구현되지 않은 행동 모델·문·소화기 기능까지 포함한 목표 설계다. 아래 표에서 **현재 구현**, **목표 설계**, **검증 필요**를 구분한다. 연구 관찰값은 보정 목표로 사용하고 프로젝트 추정값은 정책 파라미터로 분리한다.

## 1. 최종 목표

화재 상황의 NPC가 소화기와 문을 단순 애니메이션 대상으로 쓰는 것이 아니라, 제한된 정보 안에서 상황을 해석하고 다른 사람의 행동에 영향을 받으며 다음 행동을 선택하도록 만든다.

최종 실행 흐름은 다음과 같다.

```text
실제 월드 상태
  → 불완전한 지각
  → 개인 기억과 위험 해석
  → 목표·의도 형성
  → 현재 보이는 행동 가능성(Affordance) 탐색
  → 안전 제약을 먼저 적용
  → 제한 합리성 기반 선택
  → 문/소화기/출구/사람과 상호작용
  → 결과를 다시 지각하고 판단 수정
```

목표는 “모든 상황에서 실제 사람과 동일한 인공지능”이라고 과장하는 것이 아니다. 화재 대피라는 제한된 문제 안에서 다음 성질을 갖는 **행동적으로 타당하고 설명 가능한 사람 모델**을 만드는 것이다.

- NPC가 월드의 모든 정보를 미리 알지 않는다.
- 같은 자극에도 개인 경험·기억·스트레스·사회적 관계에 따라 다르게 반응한다.
- 매 tick 최적해를 계산하지 않고, 행동을 어느 정도 유지하다가 의미 있는 새 단서가 생기면 재판단한다.
- 실수는 무작위 기행이 아니라 지각 실패, 잘못된 기억, 시간 압박과 인지 부하에서 발생한다.
- 안전 조건은 성격·친숙도·군중 추종보다 우선한다.
- 동일한 정책·정렬된 관측 이벤트·seed·stable ID가 주어지면 결정 결과를 재현할 수 있다. 물리 궤적과 애니메이션까지 완전히 동일한 결과는 목표로 하지 않는다.

## 2. 현재 프로젝트에서 확인한 조건

현재 `zion-fs`에는 다음 기반이 있다.

- `Observation → Belief → Intent → ActionTask → Action/Navigation` 결정 계층
- `EYUFSActionTask::InitialExtinguish`와 15/20/30초 triangular 시간 모델
- 생명 위험·공식 지시·intent 변경 시 태스크 취소
- 경보, 연기, 온도, 군중, 안내 정보가 포함된 observation
- 익숙한 출구와 가장 가까운 안전 출구 선택
- NPC별 독립 deterministic RNG stream
- JSONL 결정 trace와 timeline review
- 실내 1·2층 NPC 분산 배치

### 2.1 현재 구현과 목표 설계의 경계

| 영역 | 기준 코드 `c7c2da5` | 이번 구현 | 남은 목표 |
|---|---|---|---|
| belief | 물리 단서 최대 기본확률 + 일부 LR | 상호 배타적 severity, 정상화·정지/이동 군중·권위 신뢰 보정 | 실제 자료로 파라미터 보정 |
| 난수 | 4개 스트림 | `TaskChoice`, `InteractionError`, `Traits`를 추가하고 trace 기록 | 대규모 재현성 회귀 |
| 대피 전 행동 | 태스크와 시간 모델 일부 | 발견 전 `ContinueRoutine`, 14개 의미 행동 selector, 반복 penalty, 결과분포 방식 | 재진입 실행과 다회 Monte Carlo 보정 |
| 팀 통합 | NPC가 경로·모션을 직접 호출 | 세 종류 directive/opportunity/feedback 계약과 fallback 토글 | 각 팀 adapter 병합 |
| NPC 배치 | stable index round-robin | 변경하지 않음 | `OccupancyZone` 가중 배치와 테스트 |
| 문 | 출구 위치 마커만 존재 | 인지 opportunity와 `OpenDoor` 요청 계약 | 실제 Door/NavLink/queue/reservation |
| 소화기 | 시간 모델만 존재 | 훈련·초기 화재·소화기·퇴로 gate, 획득/진압 계약, opt-in 런타임 시연 Actor | 운영용 registry·경쟁 해소·애니메이션·연기 시나리오 연동 |
| 재현성 | NPC별 RNG 기반 부분 재현 | evidence revision, task-choice 안정성, stale feedback 차단 테스트 | 중앙집중 fixed decision step의 L2 |
| 화재 연동 | 사전 계산 `smoke_data.bin` 재생 | 변경하지 않음 | smoke fixture와 실제 데이터 E2E |

기준점은 2026-09-02 `zion-fs` commit `c7c2da5`다. 이번 작업 트리의 인지·행동·팀 계약은 `YUFSEditor Win64 Development` 빌드와 `YUFS.NPC` 자동화 8/8을 통과했다. `AYUFSFireExtinguisher`, `AYUFSSuppressibleFireSource`, `AYUFSExtinguisherDemoDirector`로 예약·획득·분사·강도 감소·완료 feedback을 화면에서 검증하는 opt-in 시연도 제공한다. 이 시연은 공통 계약 검증용이며, 상호작용 담당자의 운영용 registry·다중 NPC 경쟁 해소·Montage/IK 어댑터를 대체하지 않는다.

### 2.2 팀 병합을 위한 책임 경계

NPC는 결과를 직접 실행하는 거대한 클래스로 확장하지 않고, 의사결정 코어가 세 담당자에게 의미 단위 요청을 보내는 구조로 병합한다.

| 소유 영역 | 담당 책임 | 입력 | 출력 | 수정 우선 폴더 |
|---|---|---|---|---|
| NPC 행동·인지 | 화재 발견 전 일상, 지각·해석, 정상화·사회적 영향, intent와 task 선택 | observation, 팀 feedback, 인지된 interaction 기회 | `FYUFSBehaviorDecision` | `NPC/Cognition`, `NPC/Decision` |
| 경로 탐색 | 목적지까지 이동, 위험·폐쇄 시 우회, 도착·차단 보고 | `FYUFSNavigationDirective` | `FYUFSTeamRequestFeedback` | `NPC/Navigation`과 별도 adapter |
| 모션 | 걷기·뛰기·둘러보기·소화·문 조작을 화면에 표현 | `FYUFSMotionDirective` | 완료·실패 feedback | `NPC/Animation`과 별도 adapter |
| 상호작용 | 소화기·문·도움 대상 발견, affordance, 예약, 실행 | `FYUFSInteractionDirective` | `FYUFSInteractionOpportunitySnapshot`, feedback | 신규 `Interaction`, `Fire`, `Door` 폴더 |

공통 계약은 `NPC/Integration/YUFSTeamIntegrationTypes.h`에 두고 `UYUFSTeamIntegrationComponent`가 directive와 feedback을 중계한다. 각 담당자는 가급적 `AYUFSEvacuationNPC.cpp`를 직접 수정하지 않고 자신의 adapter component에서 delegate를 구독한다. 기존 경로·애니메이션은 fallback으로 남기며 담당 모듈이 directive와 feedback을 모두 연결한 뒤에만 `bUseExternalNavigationDriver` 또는 `bUseExternalMotionDriver`를 켠다.

상호작용 담당이 보내는 opportunity는 NPC가 직접 보거나 듣거나 기억한 정보만 포함해야 한다. 숨겨진 문 잠금, 벽 너머 화재, 보지 못한 소화기 같은 월드 truth를 넣으면 현실적 인지 모델이 무력화되므로 금지한다.

현재 문 관련 구현은 `AYUFSExitPoint` 위치 마커뿐이다. 문짝, 잠금, 열림 각도, 통과 가능 여부, 대기열 또는 NavLink 연결은 없다.

또한 화재는 `AYUFSHeterogeneousVolume`과 `AYUFSBinaryManager`가 `smoke_data.bin`의 사전 계산 프레임을 재생하는 구조다. 런타임에 문을 열거나 소화기로 진압해도 기존 연기장을 즉시 재계산할 수 없다.

이 두 제약을 최종 설계의 출발점으로 삼는다.

## 3. 설계 원칙

### 3.1 기존 ONNX V1 계약을 깨지 않는다

현재 `EYUFSAction` 11개는 ONNX 출력 인덱스, 애니메이션 미리보기와 테스트에 연결돼 있다. 문 열기와 소화기 사용을 단순히 12·13번째 action으로 추가하면 모델 계약이 달라진다.

따라서 다음 두 계층을 분리한다.

```text
Intent / 기존 Policy Action
        │
        ├─ 이동·대기·도움 등 기존 행동
        │
        └─ 상호작용이 필요한 계획
             ↓
        Interaction Executor
        Reserve → Approach → Align → Execute → Verify → Release
```

- `EYUFSAction`: ONNX V1 호환을 위해 11개 유지
- `EYUFSActionTask`: 장시간 의미 행동 유지
- `EYUFSInteractionType`: 문, 소화기 등 월드 객체 조작 종류
- `EYUFSInteractionPhase`: 접근부터 완료/중단까지의 실행 단계

추후 ONNX V2를 학습할 때만 입력/출력 스키마를 명시적으로 버전업한다.

다만 28개 입력과 11개 출력의 **형식**이 같아도 belief·체류시간·행동 분포가 바뀌면 ONNX가 보는 의미와 분포는 달라진다. 재검증 전에는 대피 commit을 rule-based 계층이 전담하고, ONNX는 `CommitEvac` 이후의 기존 호환 행동·경로 제안에만 제한한다. 새 interaction은 안전 mask와 rule-based executor를 항상 통과한다.

### 3.2 세계의 실제 상태와 NPC가 믿는 상태를 분리한다

문이 실제로 잠겨 있어도 NPC가 멀리서 그 사실을 알면 안 된다. 소화기가 고갈됐어도 게이지를 보거나 사용해 보기 전까지 모를 수 있다.

```text
World Truth: Door=Locked
NPC A Memory: Door=Unknown, Familiarity=.9
NPC B Memory: Door=Locked, Confidence=.8, SeenAt=12.5s
```

경로 선택은 World Truth를 직접 읽지 않고 NPC의 knowledge snapshot을 사용한다. 물리적 실행 직전에는 서버/월드의 실제 상태가 최종 검증한다.

### 3.3 확률은 매 tick 룰렛이 아니다

확률 추첨은 다음 event에서만 수행한다.

- 새 경보·연기·열·불꽃을 지각
- 문이 잠김/막힘/열림으로 바뀜
- 문 너머 위험을 새로 발견
- 공식 안내나 신뢰하는 사람의 말을 수신
- 현재 태스크 완료 또는 실패
- 그룹 구성원과 분리
- 안전 경로 상실 또는 복구

행동 중간에는 commitment hysteresis를 적용해 사소한 점수 변화로 문과 경로를 계속 바꾸지 않는다.

### 3.4 안전 제약과 인간 행동을 구분한다

현실적인 사람이 항상 안전하게 행동하는 것은 아니다. 그러나 시뮬레이션 엔진의 오류로 NPC가 벽을 통과하거나 이미 확인된 치명 경로를 선택하는 것은 인간 실수가 아니다.

- **물리 불가능**: 항상 mask
- **NPC가 확인한 치명 위험**: 원칙적으로 mask
- **NPC가 아직 모르는 위험**: 선택할 수 있으나 발견 즉시 재판단
- **인지 편향·잘못된 기억**: belief와 utility를 왜곡
- **실행 실수**: 손잡이 방향 오해, 지연, 한 번 재시도 등 제한된 결과

### 3.5 근거·정책·시나리오를 분리한다

수치 하나를 코드에 직접 박아 넣지 않는다. 동일한 실행 결과를 다시 설명하고 보정할 수 있도록 세 종류의 DataAsset과 해시를 분리한다.

| 계층 | 내용 | 예시 | 변경 시 영향 |
|---|---|---|---|
| `EvidenceProfile` | 관찰 연구에서 가져온 범위·분모·대상·출처 | 대피 전 행동 수, 정보 탐색 유형, 친숙 경로 관찰 범위 | 정책을 자동 변경하지 않음 |
| `BehaviorPolicy` | 시뮬레이션에서 사용하는 prior·LR·utility·시간분포 | 경보 기본확률, 사회적 보정, 행동 지속시간 | `policyHash` 변경 |
| `ScenarioProfile` | 건물·인원·훈련·화재·문·소화기 조건 | 강의실 점유율, 직원 비율, 잠긴 문, 발화 위치 | `scenarioHash` 변경 |

각 실행 로그에는 `evidenceVersion`, `policyHash`, `scenarioHash`, `codeCommit`, `seed`를 기록한다. `policyHash`와 `scenarioHash`는 필드 순서를 정규화한 직렬화에 SHA-256을 적용한다. CRC32는 화면용 짧은 식별자로만 사용할 수 있으며 무결성 식별자로 사용하지 않는다.

### 3.6 확률을 행동 상태로 연결하는 규칙

확률은 NPC에게 행동을 매 tick 강요하는 값이 아니라, **새로운 근거가 생긴 appraisal 시점에 의도를 선택하는 조건부 값**이다.

#### 3.6.1 물리 단서는 하나의 심각도 단계로 선택한다

경보·연기·고열은 서로 독립된 동전 던지기가 아니라 중첩되는 위험 단계다. 따라서 동시에 곱하지 않고 NPC가 인지한 가장 높은 단계를 하나만 선택한다.

| `PerceivedPhysicalSeverity` | 초기 `pCommit` | 등급 | 의미 |
|---|---:|:---:|---|
| `None` | 0.05 | C | 무단서·프로젝트 prior |
| `AmbiguousAlarm` | 0.25 | C | 경보는 들었지만 화재 확신이 낮음 |
| `ConfirmedSmoke` | 0.65 | C | 연기·냄새·간접 시각 단서로 화재를 확인 |
| `ImmediateLifeThreat` | 0.90 | C | 불꽃·고열·치명 연기 등 즉시 위험 |

0.05/0.25/0.65/0.90은 서로 다른 연구의 사람 비율을 직접 옮긴 값이 아니라 프로젝트 초기 prior다. 특히 `ConfirmedSmoke`와 `ImmediateLifeThreat`는 원자료의 상태 존재 근거를 토대로 보정해야 한다.

#### 3.6.2 사회·개인 요인은 제한적으로 보정한다

```text
logit(pCommit)
  = logit(pPhysical)
  + wTraining
  + wAuthority
  + wMovingCrowd
  - wStationaryCrowd
  - wNormalcy
```

- 각 `w`는 문헌에서 직접 얻은 확정 LR이 아니라 `BehaviorPolicy`의 C등급 보정 파라미터다.
- 공식 직원의 명시적 대피 지시와 이미 확인한 생명 위험은 확률 gate를 우회한다.
- 같은 원인에서 나온 안내와 군중 이동은 중복 증거로 판단해 더 약한 쪽을 감쇠한다.
- 관찰하지 못한 사람·문·연기 정보는 계산에 넣지 않는다.
- 최종 확률은 수치 안정성을 위해 `[0.02, 0.95]`로 제한하되, 이 clamp도 보정 대상이다.

#### 3.6.3 appraisal은 evidence revision마다 한 번만 실행한다

```text
새 cue 또는 태스크 결과
  → evidenceRevision 증가
  → belief 갱신
  → 안전 인터럽트 확인
  → 아직 commit하지 않았다면 decision stream 1회 draw
  → CommitEvac 또는 PreAction/Observe 의도 확정
  → 같은 revision에서는 재추첨 금지
```

대피 전 행동의 종류는 별도 `TaskChoice` 스트림으로 뽑는다. 행동 시간은 `TaskDuration`, 경로 동률은 `Route`, 제한된 조작 실수는 `InteractionError` 스트림을 사용한다. 기능을 추가해도 기존 결정 난수열이 밀리지 않도록 소비 목적마다 스트림을 분리한다.

#### 3.6.4 안전 인터럽트는 보정 대상이 아니다

치명 연기·고열, 현재 경로 붕괴, 확인된 공식 대피 지시, 행동불능은 행동 횟수 분포를 맞추기 위해 약화하거나 빈도를 조절하지 않는다. 관찰 행동 수와 결과가 다르면 태스크 utility·인지 모델·표본 조건을 보정하고, 안전 인터럽트는 불변조건으로 유지한다.

## 4. 전체 C++ 구조

```text
AYUFSEvacuationNPC
 ├─ UYUFSNPCPerceptionComponent
 ├─ UYUFSBeliefComponent
 ├─ UYUFSIntentComponent
 ├─ UYUFSHumanCognitionComponent          신규
 ├─ UYUFSActionTaskComponent
 ├─ UYUFSInteractionExecutorComponent    신규
 ├─ UYUFSFireResponseComponent           신규
 ├─ UYUFSSmokeAwareNavigator
 └─ UYUFSActionAnimationComponent

UWorldSubsystem
 ├─ UYUFSInteractableRegistrySubsystem   신규
 └─ UYUFSInteractionResolverSubsystem    신규

World Actors
 ├─ AYUFSDoor                            신규
 ├─ AYUFSFireExtinguisher                신규
 ├─ AYUFSSuppressibleFireSource          신규
 └─ AYUFSExitPoint                       기존, Door 연결 추가
```

## 5. 범용 상호작용 프레임워크

### 5.1 공통 타입

신규 파일: `Source/YUFS/Interaction/YUFSInteractionTypes.h`

```cpp
UENUM(BlueprintType)
enum class EYUFSInteractionType : uint8
{
    None,
    OpenDoor,
    CloseDoor,
    HoldDoor,
    PassDoor,
    InspectDoor,
    PickUpExtinguisher,
    UseExtinguisher,
    DropExtinguisher
};

UENUM(BlueprintType)
enum class EYUFSInteractionPhase : uint8
{
    None,
    Proposed,
    WaitingForReservation,
    Navigating,
    Aligning,
    Executing,
    Verifying,
    Completed,
    Aborted
};

USTRUCT(BlueprintType)
struct FYUFSInteractionAffordance
{
    GENERATED_BODY()

    UPROPERTY() FGuid TargetStableId;
    UPROPERTY() EYUFSInteractionType Type = EYUFSInteractionType::None;
    UPROPERTY() FVector UseLocation = FVector::ZeroVector;
    UPROPERTY() FVector FacingDirection = FVector::ForwardVector;
    UPROPERTY() float ExpectedDuration = 0.f;
    UPROPERTY() float ExpectedRisk = 0.f;
    UPROPERTY() float ExpectedProgress = 0.f;
    UPROPERTY() uint32 RequiredCapabilityMask = 0;
    UPROPERTY() uint32 KnowledgeFlags = 0;
};
```

### 5.2 상호작용 인터페이스

신규 파일: `Source/YUFS/Interaction/YUFSInteractable.h`

```cpp
UINTERFACE(BlueprintType)
class UYUFSInteractable : public UInterface
{
    GENERATED_BODY()
};

class IYUFSInteractable
{
    GENERATED_BODY()
public:
    virtual void QueryAffordances(
        const FYUFSInteractionQuery& Query,
        TArray<FYUFSInteractionAffordance>& OutAffordances) const = 0;

    virtual EYUFSCommitResult CommitReservation(
        const FYUFSInteractionReservation& Reservation) = 0;

    virtual FYUFSInteractionResult ExecuteInteraction(
        const FYUFSInteractionContext& Context,
        float FixedDeltaSeconds) = 0;

    virtual void ReleaseReservation(uint64 Token, EYUFSAbortReason Reason) = 0;
};
```

### 5.3 Registry와 전역 충돌 해결

`UYUFSInteractableRegistrySubsystem`은 문·소화기·발화원을 등록한다. 매 tick `GetAllActorsOfClass` 또는 `TActorIterator`를 실행하지 않는다.

`UYUFSInteractionResolverSubsystem`은 같은 decision tick에 제출된 요청을 한 번에 해결한다.

```text
NPC별 snapshot
  → 각 NPC가 proposal 제출
  → stable object ID로 그룹화
  → safety/utility/path cost/NPC stable ID로 정렬
  → capacity 안에서 commit
  → 다음 fixed tick부터 실행
```

이 구조는 먼저 tick된 NPC가 항상 문이나 소화기를 차지하는 update-order 편향을 막는다.

### 5.4 일반 실행기

신규 파일: `Source/YUFS/Interaction/YUFSInteractionExecutorComponent.h/.cpp`

```cpp
bool Propose(const FYUFSInteractionAffordance& Affordance);
void OnReservationResolved(const FYUFSReservationResult& Result);
void TickInteraction(float FixedDeltaSeconds);
void Abort(EYUFSAbortReason Reason);
bool IsInteractionActive() const;
```

공통 흐름:

```text
Proposed
  → WaitingForReservation
  → Navigating
  → Aligning
  → Executing
  → Verifying
  → Completed
```

모든 단계에서 NPC 행동불능, 생명 위험, target 파괴, simulation reset을 먼저 검사한다.

## 6. 현실적 인간 사고 모델

### 6.1 개인 특성은 고정 성격 유형이 아니라 연속 변수다

신규 파일: `Source/YUFS/NPC/Cognition/YUFSHumanTraits.h`

```cpp
USTRUCT(BlueprintType)
struct FYUFSHumanTraits
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) float BuildingFamiliarity = 0.5f;
    UPROPERTY(EditAnywhere) float FireTraining = 0.f;
    UPROPERTY(EditAnywhere) float PhysicalMobility = 1.f;
    UPROPERTY(EditAnywhere) float UpperBodyStrength = 0.7f;
    UPROPERTY(EditAnywhere) float VisionAbility = 1.f;
    UPROPERTY(EditAnywhere) float StressSensitivity = 0.5f;
    UPROPERTY(EditAnywhere) float RiskTolerance = 0.3f;
    UPROPERTY(EditAnywhere) float AuthorityTrust = 0.6f;
    UPROPERTY(EditAnywhere) float SocialConformity = 0.5f;
    UPROPERTY(EditAnywhere) float HelpingTendency = 0.4f;
    UPROPERTY(EditAnywhere) float GroupAttachment = 0.5f;
    UPROPERTY(EditAnywhere) float HabitStrength = 0.5f;
};
```

이 값은 행동을 직접 결정하지 않고 지각, 기억, 해석, 행동 효용에 서로 다른 정도로 영향을 준다. “용감형은 항상 소화, 겁쟁이형은 항상 도망” 같은 단순 프로필은 사용하지 않는다.

### 6.2 인지 상태

신규 파일: `Source/YUFS/NPC/Cognition/YUFSCognitiveState.h`

```cpp
USTRUCT()
struct FYUFSCognitiveState
{
    GENERATED_BODY()

    float PerceivedRisk = 0.f;
    float Urgency = 0.f;
    float Stress = 0.f;
    float CognitiveLoad = 0.f;
    float SituationConfidence = 0.f;
    float CurrentPlanCommitment = 0.f;
    float TimeSinceLastAppraisal = 0.f;
    EYUFSIntent CurrentIntent = EYUFSIntent::Observe;
};
```

스트레스는 다음처럼 양면적으로 작동한다.

- 중간 스트레스: 반응을 빠르게 하고 대피 행동을 촉진
- 높은 스트레스: 시야·작업 기억·선택지 탐색 감소, 조작 지연과 오류 증가
- 생명 위험: 느린 숙고를 중단하고 안전한 탈출/대피를 우선

### 6.3 제한된 지각

NPC가 문과 소화기를 알게 되는 경로를 구분한다.

- 직접 시야와 line trace
- 가까운 표지판
- 과거 방문 기억
- 직원·리더의 안내
- 다른 NPC가 사용하거나 통과하는 모습
- 손잡이를 직접 시도해 얻은 정보

가시거리는 연기와 시야 능력으로 감소한다. 벽 뒤의 문, 문 너머 화재, 다른 층의 소화기 상태는 자동으로 알 수 없다.

### 6.4 기억과 신뢰도

```cpp
USTRUCT()
struct FYUFSKnownObjectState
{
    GENERATED_BODY()

    FGuid ObjectId;
    EYUFSKnownObjectType Type;
    FVector LastKnownLocation;
    int32 BelievedState;
    float Confidence;
    float Familiarity;
    float LastObservedSimTime;
    EYUFSInformationSource Source;
};
```

- 직접 확인 정보는 높은 신뢰도
- 직원 안내는 `AuthorityTrust`에 따라 신뢰도 조정
- 군중 행동은 `SocialConformity`에 따라 조정
- 오래된 정보는 서서히 신뢰도 감소
- 문이 잠겼다는 직접 경험은 해당 run 동안 강하게 기억
- 근처 NPC가 “저 문 잠겼다”는 정보를 전달할 수 있으나 전달 오류와 신뢰도를 별도 기록

### 6.5 지각–해석–결정–행동 순환

NIST의 인간 행동 모델처럼 환경 변화가 곧바로 action으로 변환되지 않게 한다.

```text
Perceive: 경보, 연기, 문, 표지판, 사람을 일부만 감지
Interpret: 오작동/실제 화재/개인 위험/타인 위험으로 해석
Decide: 확인, 대기, 도움, 소화, 문 통과, 대피 중 하나를 선택
Act: 행동 실행
Observe outcome: 문 잠김, 연기 발견, 소화 실패 등 새 단서 생성
Reappraise: 필요할 때만 다시 판단
```

### 6.6 정상화 편향과 사회적 증거

- 경보만 있고 주변이 정지해 있으면 위험 해석 신뢰도가 느리게 오른다.
- 다른 사람이 대피하거나 문을 열기 시작하면 사회적 cue가 된다.
- 공식 안내, 직접 연기, 불꽃은 정상화 편향을 빠르게 약화시킨다.
- 군중을 보지 못했으면 군중 정보를 쓰지 않는다.
- 근처 다수가 잘못된 정문으로 향하면 일부 NPC도 따라갈 수 있지만, 확인된 치명 위험을 군중 선호가 덮지는 못한다.

### 6.7 행동 지연과 실수

사람처럼 보이기 위해 모든 조작을 즉시 성공시키지 않는다.

- 손잡이를 잡기 전 정렬 시간
- 문 밀기/당기기 방향을 읽는 시간
- 불명확한 문에서 잘못된 방향으로 한 번 시도
- 잠긴 문을 확인하고 주변을 보는 시간
- 다른 사람에게 길을 묻거나 따라가는 시간
- 소화기 안전핀 제거와 조준 시간

실수 확률은 별도 magic number로 고정하지 않는다. `표시 명확성`, `조명/연기`, `훈련`, `인지 부하`, `스트레스`로 계산하고 실제 관찰 자료가 생기면 보정한다.

### 6.8 기존 14개 행동양식의 구현 위치

14개 항목은 서로 배타적인 인구 유형이 아니다. 한 NPC가 정상화 편향으로 일을 계속하다가, 연기를 확인하러 이동하고, 익숙한 출구를 택한 뒤, 연기 속에서 저자세로 움직일 수 있다. 따라서 관찰 비율을 합쳐 100%로 만들지 않고 다음처럼 서로 다른 계층에 배치한다.

| # | 행동양식 | 모델 역할 | 진입 조건과 구현 | 종료·안전 조건 | 근거 사용 방식 |
|---:|---|---|---|---|---|
| 1 | 하던 일 계속 | `ContinueActivity` 대피 전 태스크 | 모호한 경보, 낮은 상황 확신, 강한 습관, 주변 정지 | 직접 연기·열, 공식 지시, 태스크 cap | A의 3~6%와 B의 넓은 범주는 맥락이 달라 직접 확률로 쓰지 않음 |
| 2 | 남 눈치 보며 정지 | belief/대기 utility 보정 | 시야 내 다수가 정지하면 정상화·대기 가중 증가 | 유의미한 비율의 이동 군중, 직접 위험, 리더 지시 | A의 88~90%는 특정 실험 조건 효과로만 사용 |
| 3 | 확인하러 가기 | `SeekInformation` 태스크 | 불확실성이 높고 접근 가능한 정보원 존재; 창문·복도·계단·방송·동료 중 알려진 후보 선택 | 위험 상승, 충분한 정보 획득, 시간 cap | A의 15~45%, E p.48~50은 후보 구성·보정 목표 |
| 4 | 초기 소화 | `AttemptSuppression` 목표 + 소화기 상호작용 | 훈련, 초기 화재, 약제 호환, 낮은 연기, 가시적 퇴로, 예약 성공을 모두 만족 | 성장 화재, 퇴로 상실, 연기·열 임계, 고갈, 실패 | A의 1~39%와 D p.48은 상태 존재 근거; 활성 확률은 조건부 보정 |
| 5 | 순간 경직 | `FreezeInterrupt` | 급격한 위험 변화와 높은 스트레스가 처리 용량을 초과 | 짧은 duration 종료 또는 생명 위험 즉시 인터럽트 | 정량 빈도 없음; 1~3초는 C등급 애니메이션·반응 prior |
| 6 | 들어온 길로 회귀 | route utility의 `Familiarity` | 평소 경로·입장 경로 기억의 신뢰도가 높을수록 가중 | 폐쇄·잠김·직접 확인한 치명 위험이면 제거 | A 55~67%, D p.53의 역사 자료는 출구 사용 보정 목표 |
| 7 | 소지품 챙기기 | `RetrieveBelongings` 태스크 | 물품 가치·거리·애착·위험 과소평가를 반영 | 직접 위험, 공식 지시, 시간 cap; 먼 역주행은 더 큰 비용 | A 11~57%, E p.47~48; 회수자 중 먼 위치 회귀 24%는 조건부 목표 |
| 8 | 다시 들어가기 | `ReenterForDependent` 별도 intent | 보호 대상·직무·위치 기억·통과 가능성이 명시된 경우만 심사 | 출입 통제, 치명 위험, 경로 단절, 목표 구조 완료 | D p.53~54의 역사적 43%를 현대 일반확률로 사용하지 않음 |
| 9 | 군집 추종 | route utility의 `SocialFlow` | 실제로 관측한 이동 군중 방향과 신뢰도를 반영 | 군중이 정지·분산하거나 위험 경로가 확인되면 약화·제거 | A의 조건 지표와 B의 20% 제안은 Monte Carlo 보정 목표 |
| 10 | 방관·구경·촬영 | `ObserveOrRecord` 저우선 태스크 | 아직 대피를 commit하지 않았고 심리적 거리·호기심이 높을 때만 후보 | 직접 위험·공식 지시·접근 인파 증가 시 즉시 취소 | 영상 단위 비율은 사람 비율이 아니므로 정량 입력 금지 |
| 11 | 리더 역할 자처 | 역할 기반 `LeadAndWarn` 태스크 | 직원·책임 역할, 높은 상황 확신, 의사소통 가능 대상 | 본인 생명 위험, 안내 불가능, 역할 인계 | A 14~21% 대리지표와 E p.68~73은 사회적 영향 검증에 사용 |
| 12 | 이타적 구조·도움 | `AssistOther` 태스크 + 대상 예약 | 도움 필요를 인지하고 접근·보조 역량과 안전 여유가 있을 때 | 대상 확보 실패, 위험 급증, 이동 불능; 한 대상 한 helper 원칙 | A 9~34%, E p.68은 조건부 빈도와 그룹 결과 보정 |
| 13 | 연기 속 저자세·벽 짚기 | `SmokeLocomotionMode` | 교육·가시거리·연기 농도·벽 접근성을 조합 | 맑은 구간 진입, 건강 악화, 벽 경로 단절 | 정량 인구 비율을 만들지 않고 절차 정확성과 속도를 검증 |
| 14 | 권위·안내 방송 기다리기 | `WaitForAuthority` 대기 사유 | 권위 신뢰가 높고 상황 확신은 낮으며 공식 메시지가 없을 때 | 대피 지시 수신 즉시 commit; 위험 직접 확인 시 취소 | A/B 11~17% 범위는 특정 조건 보정 목표 |

### 6.9 상태와 태스크의 최종 계층

```text
Top-level Intent
  Observe
  PreAction
  CommitEvac
  AssistEvac
  AttemptSuppression
  ReenterForDependent
  Shelter
  Incapacitated

PreAction Task
  ContinueActivity
  WaitObserve
  WaitForAuthority
  SeekInformation
  RetrieveBelongings
  ObserveOrRecord
  LeadAndWarn
  AssistOther

Cross-cutting Modifier / Interrupt
  NormalcyBias, SocialProof, FamiliarRoute, CrowdFollowing
  FreezeInterrupt, SmokeLocomotionMode
```

이 분리는 기존 `EYUFSAction` 11개와 ONNX 출력 계약을 바로 변경하지 않으면서도 행동 의미를 추가할 수 있게 한다. 기존 action은 표현·호환 계층으로 유지하고, 실제 장시간 과정과 객체 조작은 `Intent`, `ActionTask`, `Interaction`에 둔다.

### 6.10 대피 전 태스크 선택과 행동 수

태스크는 고정 인구 비율을 한 번 뽑아 배정하지 않는다. 현재 NPC가 알고 있는 후보만 mask한 후 제한 합리적 선택을 한다.

```text
U(task) = baseLogit
        + informationGain
        + goalValue
        + socialUtility
        - expectedTime
        - perceivedRisk
        - repetitionPenalty
        - cognitiveCost

P(task) = maskedSoftmax(U / temperature)
```

초기 `baseWeight`는 `SeekInformation .45 / Wait·Continue .20 / RetrieveBelongings .20 / Warn·Assist .10 / AttemptSuppression .05`를 C등급 prior로 사용한다. 이는 사람 비율이 아니라 **모든 후보가 활성일 때의 상대 선택 가중치**다. 문·소화기·도움 대상이 없으면 해당 후보는 제거되고 남은 후보만 재정규화한다.

- 같은 태스크의 연속 선택에는 repetition penalty와 cooldown을 준다.
- 반복 횟수 합을 10회로 막아 놓고 15회 목표를 요구하지 않는다.
- 관찰된 `1~5회 88.5% / 6~9회 8.1% / 10~15회 3.4%`는 E p.25~26의 표본 결과로서 실행 후 분포를 비교하는 calibration target이다.
- 목표 행동 수를 미리 뽑아 강제로 채우지 않는다. 대피 commit, 태스크 실패·완료, 위험 인터럽트의 경쟁 과정에서 행동 수가 결과로 나오게 한다.
- 무한 반복 방지용 기술 상한은 행동 모델 파라미터와 분리하고, 발동하면 정상 결과가 아니라 `LoopGuardTriggered` 오류로 기록한다.

## 7. 문 상호작용 상세 설계

### 7.1 문 Actor

신규 파일: `Source/YUFS/Interaction/Door/YUFSDoor.h/.cpp`

```cpp
UENUM(BlueprintType)
enum class EYUFSDoorState : uint8
{
    Closed,
    Opening,
    Open,
    Closing,
    Locked,
    Jammed,
    Blocked,
    Destroyed
};

UENUM(BlueprintType)
enum class EYUFSDoorType : uint8
{
    Normal,
    FireDoor,
    EmergencyExit,
    Automatic,
    DoubleLeaf
};
```

Actor 구성:

- `USceneComponent* FrameRoot`
- `UStaticMeshComponent* DoorLeafA`
- 선택 사항: `UStaticMeshComponent* DoorLeafB`
- `USceneComponent* HandlePointSideA/B`
- `UBoxComponent* PassageVolume`
- `UBoxComponent* QueueAreaSideA/B`
- `UNavLinkCustomComponent* SmartNavLink`
- `UNavModifierComponent* ClosedDoorModifier`
- 선택 사항: `UAudioComponent* DoorAudio`

주요 설정:

```cpp
UPROPERTY(EditAnywhere) FName DoorStableId;
UPROPERTY(EditAnywhere) EYUFSDoorType DoorType;
UPROPERTY(EditAnywhere) float ClearWidthCm = 90.f;
UPROPERTY(EditAnywhere) float MaxOpenAngleDeg = 95.f;
UPROPERTY(EditAnywhere) float OpenSeconds = 0.8f;
UPROPERTY(EditAnywhere) float CloseSeconds = 1.5f;
UPROPERTY(EditAnywhere) bool bPushFromSideA = true;
UPROPERTY(EditAnywhere) bool bSelfClosing = false;
UPROPERTY(EditAnywhere) bool bInitiallyLocked = false;
UPROPERTY(EditAnywhere) float RequiredStrength = 0.2f;
UPROPERTY(EditAnywhere) int32 PassageCapacity = 1;
```

### 7.2 문 상태와 NavMesh 연결

문 상태가 바뀔 때만 navigation을 갱신한다.

| 문 상태 | NavLink | 충돌 | 경로 의미 |
|---|---|---|---|
| Closed | 상호작용 가능한 link | 닫힘 | 도달 후 열기 필요 |
| Opening/Closing | 제한적 | 각도에 따른 충돌 | 통과 폭 동적 계산 |
| Open | 활성 | 통로 확보 | 정상 통과 |
| Locked/Jammed/Blocked | 비활성 | 막힘 | 해당 NPC가 알면 route mask |
| Destroyed | 활성 | 제거 | 정상 통과, 방연 성능 없음 |

NavMesh를 문 애니메이션 매 frame 다시 build하지 않는다. `UNavLinkCustomComponent`와 door state cost를 사용하고, 문 상태 event에서 path cache만 무효화한다.

### 7.3 문 사용 상태기계

```text
문을 경로 gateway로 선택
  → 접근 측 queue slot 예약
  → 손잡이 위치로 이동
  → 실제 문 상태 확인
  → 필요하면 밀기/당기기
  → 최소 통과 각도까지 대기
  → passage token 획득
  → 문 통과
  → token 반환
  → 필요하면 잡아주기 또는 self-close
```

세부 phase:

| 단계 | 행동 | 새 단서/재판단 |
|---|---|---|
| `ApproachQueue` | 대기열 끝으로 이동 | 줄 증가, 다른 문 개방 |
| `WaitTurn` | 앞사람과 개인 공간 유지 | 대기시간 임계 초과 |
| `Inspect` | 손잡이·표지·연기 확인 | 잠김, 반대 방향, 문틈 연기 |
| `Manipulate` | 밀기/당기기/손잡이 조작 | 실패, jam, strength 부족 |
| `Hold` | 가까운 사람을 위해 문 유지 | 생명 위험 증가 시 즉시 놓음 |
| `Pass` | 문짝 충돌을 피하며 통과 | 문 너머 연기 직접 관측 |
| `Release` | 손을 놓고 진행 | fire door self-close 시작 |

### 7.4 대기열과 통과 용량

문 전체를 한 명이 독점하는 reservation으로 구현하면 비현실적인 직렬 병목이 생긴다.

- 조작 token: 문을 여는 NPC 1명
- passage token: `ClearWidthCm`과 열린 각도에 따라 1개 이상
- queue slot: 문 양쪽에 stable index로 생성
- hold state: 문을 잡은 NPC와 통과 NPC를 분리

NPC는 줄의 마지막 slot을 예약하고 앞사람을 통과하지 않는다. 단, 넓은 양개문과 다중 passage token에서는 나란히 통과할 수 있다.

혼잡 때문에 다른 문으로 전환할 때는 다음 조건을 사용한다.

```text
expectedGain
  = currentDoorExpectedTime - alternativeDoorExpectedTime

switch when
  expectedGain > RouteSwitchThreshold
  AND alternative confidence is sufficient
  AND current commitment is not too high
```

매 tick 가장 짧은 줄로 이동하는 진동을 방지한다.

### 7.5 사람다운 문 선택

문 선택 utility는 NPC가 알고 있는 후보에 대해서만 계산한다.

```text
U(door, route)
  = - α · perceivedTravelTime
    - β · perceivedSmokeDose
    - γ · expectedQueueTime
    - η · operationDifficulty
    - θ · uncertainty
    + δ · familiarity
    + ε · trustedLeaderRecommendation
    + ζ · perceivedSignVisibility
    + κ · groupCohesion
```

이 식은 특정 논문에서 그대로 가져온 인간 행동 법칙이 아니라, 문헌에서 반복적으로 언급되는 경로 요인을 비교 가능한 항으로 분해한 **프로젝트 선택 모델**이다. α~κ는 모두 P등급이며 `BehaviorPolicy`에 둔다. 시간·연기량·대기시간처럼 단위가 다른 입력은 시나리오 기준 범위로 정규화하고, 확인된 폐쇄·물리 불가능·인지한 치명 위험은 utility 계산 전에 후보에서 mask한다. 보정 전에는 계수의 절대값이나 행동 예측 정확도를 연구 결과로 주장하지 않는다.

- 처음 들어온 정문은 familiarity가 높다.
- 가까운 비상문도 보지 못했거나 용도를 이해하지 못하면 후보가 아니다.
- 다른 사람이 문을 성공적으로 사용하면 해당 문의 신뢰도가 올라간다.
- 잠긴 문을 직접 확인하면 기억하고 경로를 다시 계산한다.
- 문 너머 연기 때문에 되돌아오면 가까운 NPC에게 경고 cue를 줄 수 있다.
- 그룹 구성원이 문 반대편에 있으면 문을 잡거나 기다릴 가능성이 올라간다.

### 7.6 밀기·당기기와 잠긴 문

NPC가 접근한 측면과 `bPushFromSideA`로 올바른 조작을 결정한다.

표시가 불명확하고 스트레스가 높으면 반대 방향으로 짧게 한 번 시도할 수 있다. 실패 후에는 촉각 정보로 belief를 갱신하고 올바른 방향으로 재시도한다.

잠긴 문 처리:

1. handle interaction 실패
2. `Locked` 확신 상승
3. 같은 문 반복 시도 금지 cooldown
4. 주변 NPC에게 실패 정보를 전달
5. 대체 경로 appraisal
6. 대체 경로가 없으면 `Shelter` 또는 도움 요청

### 7.7 방화문

방화문은 닫혀 있을 때 화재와 연기 확산을 늦추는 역할을 하므로 기본적으로 self-close한다.

- NPC가 통과할 때만 열린다.
- 가까운 뒤따르는 사람이 있으면 짧게 잡아줄 수 있다.
- 생명 위험이 높으면 문을 잡아주는 행동을 중단한다.
- 문이 닫힐 때 다른 NPC capsule이 passage volume 안에 있으면 끼임을 방지한다.
- 훈련된 직원은 지연이 크지 않을 때 수동으로 닫을 수 있다.

현재 사전 기록 연기 데이터는 문 상태에 따라 바뀌지 않으므로, 첫 구현에서 방화문의 방연 효과를 실제 smoke grid 감소로 위조하지 않는다. 물리 효과는 문 상태별 사전 계산 scenario branch가 준비됐을 때 활성화한다.

### 7.8 `AYUFSExitPoint` 연결

기존 exit marker에 다음 속성을 추가한다.

```cpp
UPROPERTY(EditAnywhere) TObjectPtr<AYUFSDoor> GatewayDoor;
UPROPERTY(EditAnywhere) bool bRequiresDoorPassage = true;
UPROPERTY(EditAnywhere) float NominalFlowPeoplePerSecond = 1.f;
```

출구가 가깝더라도 연결 문이 잠기거나 막혔으면 도달 가능한 출구가 아니다. `YUFSLevelDataManager`는 단순 직선거리 대신 door gateway 상태, NPC knowledge, smoke danger, queue delay를 반영한 후보를 제공한다.

## 8. 소화기 상호작용 상세 설계

### 8.1 소화기 수명주기

```text
Available → Reserved → Carried → Dropped
                         └──────→ Depleted
```

신규 파일:

- `Fire/YUFSFireInteractionTypes.h`
- `Fire/YUFSExtinguisherDefinition.h/.cpp`
- `Fire/YUFSFireExtinguisher.h/.cpp`
- `Fire/YUFSSuppressibleFireSource.h/.cpp`
- `NPC/Tasks/YUFSFireResponseComponent.h/.cpp`

소화기 Actor 구성:

- `UStaticMeshComponent* BodyMesh`
- `USceneComponent* InteractionPoint`
- `USceneComponent* NozzlePoint`
- `UNiagaraComponent* DischargeVfx`
- `UAudioComponent* DischargeAudio`

### 8.2 소화 행동 안전 gate

다음 조건을 모두 만족할 때만 소화 행동을 후보로 만든다.

1. NPC가 소화기 교육을 받았다.
2. 화재가 휴대용 소화기로 대응 가능한 초기 단계다.
3. 화재 종류와 소화기 약제가 호환된다.
4. 사용 가능하고 접근 가능한 소화기가 있다.
5. 소화기에서 안전 공격 위치까지 NavMesh 경로가 있다.
6. NPC 뒤쪽에 화재에서 멀어지는 안전 퇴로가 있다.
7. 현재 연기·온도·가시거리가 정책 임계치 안에 있다.
8. 공식 전면 대피 지시가 없다.
9. NPC가 행동불능·도움 대상·재진입 상태가 아니다.

초기 정책값:

| 설정 | 시작값 | 성격 |
|---|---:|---|
| `SuppressBaseWeight` | 5 | 안전 mask 후 상대 가중치, 사람 비율 아님 |
| `SuppressDuration` | 15/20/30초 | 기존 triangular 모델 |
| `MaxSuppressionSeconds` | 30초 | hard cap |
| `MaxSmokeForAttempt` | 0.15 | 프로젝트 초기값 |
| `MaxTemperatureForAttempt` | 0.35 | 프로젝트 초기값 |
| `ImmediateAbortSmoke` | 0.70 | 기존 생명 위험 임계치 |
| `ImmediateAbortTemperature` | 0.80 | 기존 생명 위험 임계치 |
| `ReservationLeaseSeconds` | 5초 | 진행 갱신 없을 때 해제 |
| `ExtinguishedIntensity` | 0.05 | 성공 판정 시작값 |
| `VerifySeconds` | 2초 | 재발화 확인 |

### 8.3 소화 실행 상태

```text
RequestingResource
  → MovingToExtinguisher
  → Equipping
  → MovingToAttackPoint
  → Aiming
  → Discharging
  → Verifying
  → Completed / Aborted
```

성공 후에도 건물을 안전하다고 단정하지 않고 `CommitEvac`으로 전환한다.

### 8.4 진압 계산

```text
effectivePower
  = agentMass
  × extinguisherPower
  × classCompatibility
  × lineOfSightFactor
  × rangeFactor
  × aimFactor

nextIntensity
  = clamp(currentIntensity + growth - effectivePower, 0, 1)
```

조준이 빗나가거나 너무 멀면 약제만 줄고 진압력은 낮아진다. 렌더 frame이 아니라 fixed simulation step에서 계산한다.

### 8.5 즉시 중단 조건

다음은 1 fixed tick 이내 소화 행동을 취소한다.

- 행동불능
- 생명 위험 연기/온도
- 공식 전면 대피 지시
- 안전 퇴로 상실 또는 연결 문 폐쇄/잠김
- 화재가 초기 단계를 벗어남
- 소화기 고갈·손실·비호환
- path 실패/장시간 정체
- 30초 hard cap

중단 시 방사·VFX·audio를 끄고 소화기를 drop 또는 안전 위치로 반환하며 `CommitEvac`, 안전 경로가 없으면 `Shelter`로 전환한다.

## 9. 문과 소화기의 결합 행동

상호작용은 서로 독립적이지 않다.

### 사례 A: 소화기함 문

```text
소화기 발견
  → 함 문이 닫혀 있음
  → OpenDoor affordance
  → 문 열기
  → PickUpExtinguisher
  → 화재 접근
```

### 사례 B: 소화 중 퇴로 문이 닫힘

```text
Discharging
  → 뒤쪽 fire door self-close
  → escape route 재검증
  → 문이 정상 개방 가능하면 계속
  → 잠김/막힘이면 즉시 Abort → 대체 경로
```

### 사례 C: 문 너머 연기 발견

```text
문 접근 당시 위험을 모름
  → 문을 조금 열어 연기 cue 획득
  → perceived risk 급상승
  → 문 닫기 또는 놓기
  → 뒤로 물러남
  → 주변 사람 경고
  → 다른 출구 재탐색
```

### 사례 D: 직원이 문을 잡아줌

```text
훈련된 직원이 비상문 개방
  → HoldDoor
  → nearby NPC의 문 신뢰도 상승
  → 군중 일부가 해당 출구로 전환
  → 위험 증가 시 직원도 통과하고 release
```

## 10. 사람처럼 보이는 행동의 구체적 결과

개별 행동을 미리 배정하지 않고 특성·정보·상황의 조합으로 다음 결과가 나오게 한다.

### 익숙한 학생

- 경보만 들으면 주변 반응을 잠깐 확인
- 평소 사용한 정문 기억의 신뢰도가 높음
- 가까운 비상문을 보지 못하면 정문으로 이동
- 정문 줄이 길고 직원이 다른 문을 안내하면 경로 변경 가능

### 처음 방문한 사람

- 공간 기억이 거의 없음
- 표지판과 주변 사람의 이동을 더 많이 사용
- 닫힌 비상문을 출구로 인식하지 못할 수 있음
- 다른 사람이 문을 열고 통과하는 모습을 보면 후보에 추가

### 훈련된 직원

- 경보의 신뢰도를 높게 해석
- 주변 사람에게 알리고 비상문을 개방
- 초기 화재·적절한 소화기·안전한 퇴로가 모두 있을 때만 소화 시도
- 방화문을 불필요하게 열린 채 두지 않음

### 스트레스가 높은 NPC

- 의사결정 시간이 짧아질 수 있지만 선택지 탐색 범위가 좁음
- 익숙한 경로와 군중 cue에 더 의존
- 불명확한 문을 잘못된 방향으로 한 번 조작할 가능성 증가
- 직접 연기나 막힌 문을 확인하면 계획을 급히 전환

### 그룹 구성원

- 혼자 최적 문으로 가지 않고 가까운 구성원을 기다릴 수 있음
- 문을 잡아주거나 뒤처진 사람에게 경고
- 생명 위험이 강해지면 그룹 결속보다 탈출이 우선될 수 있음

## 11. 경로 선택·초기 배치·문 gateway

### 11.1 경로 선택과 문 gateway

기존 경로 효용을 door gateway까지 확장한다.

```text
Route candidate
  = Nav path
  + ordered door gateways
  + expected queues
  + NPC-known hazards
  + destination exit
```

`travelTime`에는 다음을 포함한다.

- 걷기 시간
- 계단/저자세 이동 시간
- 문 접근 queue 대기
- 문 여는 예상 시간
- 통과 용량 지연
- 그룹 구성원 대기

경로 재탐색 event:

- 문 잠김·jam·blocked 확인
- queue 예상시간 급증
- 문 너머 연기 발견
- 신뢰할 수 있는 안내
- 그룹 분리
- 발화원 성장
- 소화 행동 중단

### 11.2 NPC 초기 분산 배치

층별 NavMesh 면적만으로 사람 수를 배분하면 복도·계단·창고가 재실 공간으로 과대 계산된다. 다음 우선순위로 점유 가중치를 정한다.

```text
1순위: 시나리오에 배치된 AYUFSOccupancyZone
2순위: 방/공간 메타데이터의 usable area × occupancy density
3순위: 층별 walkable area × FloorWeightOverride
4순위: 현재 round-robin (개발용 fallback만 허용)
```

```cpp
USTRUCT(BlueprintType)
struct FYUFSOccupancyZoneSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) FName ZoneStableId;
    UPROPERTY(EditAnywhere) int32 FloorIndex = 0;
    UPROPERTY(EditAnywhere) float OccupancyWeight = 1.f;
    UPROPERTY(EditAnywhere) int32 Capacity = 0;
    UPROPERTY(EditAnywhere) EYUFSRoomUse RoomUse = EYUFSRoomUse::Unknown;
    UPROPERTY(EditAnywhere) bool bAllowInitialSpawn = true;
};
```

정수 인원 배분은 deterministic weighted rounding을 사용한다.

1. 각 zone의 이상적 인원 `q_i = N × w_i / Σw`를 계산한다.
2. `floor(q_i)`를 우선 배정한다.
3. 남은 인원은 소수부가 큰 zone부터 배정하고 동률은 `ZoneStableId`로 해소한다.
4. zone capacity를 넘으면 초과분을 다음 유효 zone에 같은 규칙으로 재배분한다.
5. NPC stable ID 오름차순으로 확정된 zone에 넣고, zone 안에서는 Poisson-disc 또는 blue-noise 후보를 사용한다.

이 방법은 Sainte-Laguë와 최대잉여법을 혼합해 부르는 모호함을 피하고, 소수 인원에서도 배분 근거를 설명할 수 있다.

배치 후보는 다음을 모두 통과해야 한다.

- static floor hit와 기대 층 높이 일치
- NavMesh projection 성공과 완전한 capsule fit
- 다른 NPC와 최소 간격 충족
- `OccupancyZone` 내부이고 문짝 sweep·계단 낙하·화재 시작 exclusion volume 밖
- 천장 충돌로 실내 확인한 A등급 또는 제한 거리 내 다방향 enclosure로 확인한 B등급

B등급은 실내라는 사실을 증명하지 않으므로 결과 로그에 `indoorValidationGrade`를 남긴다. B등급 비율 경고 임계값은 프로젝트 설정으로 두고, 여러 CAD 맵에서 측정하기 전 특정 수치를 품질 기준으로 고정하지 않는다. 후보 상한을 256에서 32로 줄이는 최적화도 평균·p95 배치 시간과 실패율을 벤치마크한 뒤 적용한다.

## 12. 애니메이션과 물리 표현

### 문

- 손잡이 reach/turn montage
- push/pull montage
- 문짝과 손의 IK target
- passage 중 capsule–door 충돌 회피
- hold-door pose
- jam/locked 반응 animation

### 소화기

- 집기 montage
- 오른손 소화기 socket
- 왼손 nozzle IK
- 안전핀 제거·조준 montage
- 방사 loop montage
- 약제 Niagara와 sound cue

상태 변경은 C++ fixed-step이 authoritative하다. Animation notify는 시각적 동기화에 사용하지만 headless 테스트의 성공 조건으로 사용하지 않는다.

문짝은 Chaos 완전 물리보다 deterministic kinematic hinge를 기본으로 한다. 시각 회전은 부드럽게 보간하되 논리 open angle과 통과 가능 폭은 fixed-step 값으로 계산한다.

## 13. 화재·연기 데이터 일관성

### 현재 가능한 수준

- 문 열림/닫힘이 collision, navigation, queue, line of sight에 실제 영향
- 소화 성공이 논리적 발화원 intensity와 flame VFX에 영향
- 기존 smoke/temperature data는 잔류 위험으로 계속 사용
- 소화 성공이나 문 닫힘이 연기장을 즉시 0으로 만들지 않음

### 최종 물리 연동

사전 계산 branch를 준비한다.

```text
Scenario branch examples
  ├─ DoorsClosed_NoSuppression
  ├─ DoorAOpen_NoSuppression
  ├─ DoorsClosed_SuppressedAt10s
  └─ DoorAOpen_SuppressedAt20s
```

모든 문 조합을 계산하면 경우의 수가 폭발하므로 화재 해석상 중요한 방화문과 진압 시점만 branch로 만든다. `scenarioHash`에 door state branch와 suppression branch를 포함한다.

branch가 없는 문 상태에서는 방연 효과를 임의로 만들지 않고 “behavior-only interaction”으로 로그에 표시한다.

## 14. 로그와 타임라인

### NPC cognition trace

- 감지한 cue와 실제 cue 구분
- cue source와 confidence
- perceived risk/stress/cognitive load
- 기억한 문·소화기 상태
- 고려한 affordance 목록
- mask 이유
- utility 구성요소
- 선택 확률과 RNG counter
- 계획 변경 trigger

### Interaction trace

- `interactionType`
- `targetStableId`
- `fromPhase/toPhase`
- `reservationTokenHash`
- `queueIndex`
- `doorState/openAngle`
- `extinguisherState/remainingAgent`
- `fireIntensityBefore/After`
- `abortReason`

### Timeline snapshot

NPC만 기록하면 과거 시점에서 손에 든 소화기가 벽에 다시 나타나거나, NPC가 닫힌 문을 통과하는 불일치가 생긴다. 다음 world state를 함께 기록한다.

- 문 state, angle, lock/jam, holder, passage owners
- 소화기 state, 위치, 잔량, owner
- 발화원 intensity/stage/suppressed
- interaction reservation generation

### 재현성 계약과 로그 수명주기

| 등급 | 보장 범위 | 조건 | 목표 |
|---|---|---|---|
| L1 결정 재현 | 동일 appraisal에서 동일 확률·draw·intent·task | 동일 policy/scenario hash, seed, stable ID, 정렬된 관측 이벤트 | 필수 |
| L2 이벤트 재현 | 두 실행의 canonical JSONL 이벤트가 동일 | controller가 stable ID 순으로 decision step 호출, 시간·부동소수 정규화 | 필수 |
| L3 궤적 재현 | 모든 시각의 위치·애니메이션·물리 결과 동일 | 결정적 물리·NavMesh·tick 전체가 필요 | 범위 제외 |

로그는 tick마다 모든 상태를 쓰지 않고 `Appraisal`, `IntentChanged`, `TaskChanged`, `InteractionChanged`, `RouteChanged`, `HealthThresholdCrossed` 이벤트 중심으로 기록한다. 용량 산정은 실제 20/200/500 NPC trace의 평균·p95 라인 수와 바이트를 측정한 뒤 확정한다.

- 비동기 writer는 bounded queue와 8~64 KB 배치 버퍼를 사용한다.
- 정상 종료와 PIE 중단에서는 flush와 파일 close를 완료한다.
- hard crash에서 마지막 버퍼 flush는 보장할 수 없으므로 각 JSONL 행을 독립 파싱 가능하게 만들고 주기적으로 flush한다.
- 분석기는 잘린 마지막 행을 무시하고 `runCompleted=false`로 표시한다.
- `paramsHash`만 비교하지 않고 `policyHash`, `scenarioHash`, `codeCommit`, source revision을 함께 비교한다.

## 15. 예상 파일 구조

### 신규

- `Interaction/YUFSInteractionTypes.h`
- `Interaction/YUFSInteractable.h`
- `Interaction/YUFSInteractableRegistrySubsystem.h/.cpp`
- `Interaction/YUFSInteractionResolverSubsystem.h/.cpp`
- `Interaction/YUFSInteractionExecutorComponent.h/.cpp`
- `Interaction/Door/YUFSDoor.h/.cpp`
- `Interaction/Door/YUFSDoorDefinition.h/.cpp`
- `NPC/Cognition/YUFSHumanCognitionTypes.h`
- `NPC/Cognition/YUFSHumanCognitionComponent.h/.cpp`
- `NPC/Cognition/YUFSBehaviorPolicy.h/.cpp`
- `NPC/Decision/YUFSHumanBehaviorSelectorComponent.h/.cpp`
- `NPC/Integration/YUFSTeamIntegrationTypes.h`
- `NPC/Integration/YUFSTeamIntegrationComponent.h/.cpp`
- `NPC/Decision/YUFSEvidenceProfile.h/.cpp`
- `Fire/YUFSFireInteractionTypes.h`
- `Fire/YUFSExtinguisherDefinition.h/.cpp`
- `Fire/YUFSFireExtinguisher.h/.cpp`
- `Fire/YUFSSuppressibleFireSource.h/.cpp`
- `NPC/Tasks/YUFSFireResponseComponent.h/.cpp`
- `Level/YUFSOccupancyZone.h/.cpp`
- `Tests/YUFSInteractionFrameworkTests.cpp`
- `Tests/YUFSBeliefPolicyTests.cpp`
- `Tests/YUFSOccupancyDistributionTests.cpp`
- `Tests/YUFSDoorInteractionTests.cpp`
- `Tests/YUFSFireExtinguisherTests.cpp`
- `Tests/YUFSHumanBehaviorFunctionalTests.cpp`

### 수정

- `Core/YUFSTypes.h`
- `Core/YUFSObservation.h/.cpp`
- `Core/YUFSDecisionTraceLogger.h/.cpp`
- `Level/YUFSExitPoint.h/.cpp`
- `Level/YUFSLevelDataManager.h/.cpp`
- `NPC/YUFSEvacuationNPC.h/.cpp`
- `NPC/Decision/YUFSBeliefComponent.h/.cpp`
- `NPC/Decision/YUFSIntentComponent.h/.cpp`
- `NPC/Tasks/YUFSActionTaskComponent.h/.cpp`
- `NPC/Navigation/YUFSSmokeAwareNavigator.h/.cpp`
- `NPC/Animation/YUFSActionAnimationComponent.h/.cpp`
- `Simulation/YUFSTimelineTypes.h`
- `Simulation/YUFSTimelineRecorder.h/.cpp`
- `Simulation/YUFSSimulationController.h/.cpp`

## 16. 테스트 계획

### belief·상태 전이

1. `None/Alarm/Smoke/LifeThreat`가 각각 하나의 상호 배타적 물리 심각도만 사용한다.
2. 위험 심각도가 상승하면 다른 입력이 같을 때 `pCommit`이 감소하지 않는다.
3. 동일 `evidenceRevision`에서 decision draw가 한 번만 소비된다.
4. `TaskChoice` draw가 늘어도 `Decision`과 `Route` 스트림 결과가 변하지 않는다.
5. 공식 대피 지시와 생명 위험이 확률 gate 및 대피 전 태스크를 우회한다.
6. 모든 후보가 mask되면 임의 인덱스를 선택하지 않고 `Shelter` 또는 명시적 fallback으로 전이한다.
7. 현재 max-base 정책과 목표 정책의 Monte Carlo 결과를 같은 시나리오에서 비교해 의미 변화 보고서를 남긴다.

### 태스크·14개 행동

1. 14개 행동 각각이 주 상태·태스크·경로 보정·인터럽트·이동 방식 중 정확히 하나의 주 책임에 연결된다.
2. 사회적 증거와 군집 추종이 동일 cue를 두 번 확률 가산하지 않는다.
3. 반복 penalty와 cooldown 이후에도 후보가 없으면 정상적으로 재평가한다.
4. 생명 위험 인터럽트는 행동 횟수 보정과 무관하게 1 fixed decision step 안에 처리된다.
5. 1~5/6~9/10~15회 분포는 다회 실행 결과로만 비교하고 NPC별 목표 횟수를 강제하지 않는다.
6. 재진입은 명시적 보호 대상·직무·기억·경로가 없으면 후보 자체가 생성되지 않는다.

### 초기 배치

1. 동일 zone weight와 seed에서 zone별 정원과 stable NPC 할당이 같다.
2. 4:1, 1:1 홀수 N, 3개 층, capacity overflow 배분을 검증한다.
3. corridor·stair·fire exclusion zone에 초기 NPC가 생성되지 않는다.
4. A/B 실내 판정 비율과 NPC당 후보 시도 횟수를 리포트한다.
5. 20/100/500 NPC에서 배치 시간의 평균과 p95를 기록한 뒤 후보 상한을 결정한다.

### 범용 interaction

1. 같은 seed와 snapshot에서 같은 reservation 결과가 나온다.
2. NPC update 순서를 섞어도 승자와 queue 순서가 같다.
3. generation이 다른 오래된 callback은 거부된다.
4. target 파괴/reset 시 모든 reservation이 해제된다.
5. 물리적으로 불가능한 affordance는 항상 mask된다.

### 문

1. 닫힌 문은 접근 후 열고 통과한다.
2. 열린 문은 불필요한 조작 없이 통과한다.
3. 잠긴 문 확인 후 같은 문 무한 재시도를 하지 않는다.
4. 문 상태가 바뀌면 관련 path cache만 무효화된다.
5. 방화문은 통과 후 self-close한다.
6. passage volume 안에 NPC가 있으면 문이 capsule을 관통하지 않는다.
7. 20명이 한 문에 접근해도 겹치지 않고 stable queue를 형성한다.
8. 양개문은 설정된 passage capacity를 사용한다.
9. 문 너머 치명 연기를 발견하면 통과 전 되돌아간다.
10. 대체 출구가 없으면 `Shelter`로 전환한다.

### 소화기

1. 두 NPC가 한 소화기를 동시에 소유할 수 없다.
2. 비호환 소화기는 후보에서 제외된다.
3. 잔량 0에서는 진압력이 적용되지 않는다.
4. range/angle/LOS 밖에서는 진압력이 적용되지 않는다.
5. 치명 연기·공식 지시·퇴로 상실 시 1 tick 안에 중단한다.
6. 소화 성공 후 `CommitEvac`으로 전환한다.

### 인간 행동

1. 보지 못한 비상문을 omniscient하게 선택하지 않는다.
2. 직접 잠김을 확인한 NPC만 즉시 해당 정보를 안다.
3. 정보 전달을 받은 NPC는 source trust에 따라 belief를 갱신한다.
4. 경보만 있고 주변이 정지한 조건에서 정상화 편향이 나타난다.
5. 직접 연기/열 cue가 생기면 지연 행동이 중단된다.
6. 익숙한 출구 선호가 나타나되 확인된 위험을 덮지 않는다.
7. 작은 utility 변동으로 경로가 매 tick 진동하지 않는다.
8. 높은 스트레스에서 선택지 탐색 감소와 조작 지연이 나타난다.
9. 모든 NPC가 동일한 행동 순서를 복제하지 않는다.
10. 동일 policy/scenario·정렬 관측 이벤트·seed 실행의 canonical trace hash가 일치한다.

### 검증 시나리오

- 모호한 경보 + 정지 군중에서 계속하기·관찰 대기와 사회적 증거가 나타나는 장면
- 같은 경보에서 이동 군중·직원 지시가 추가되며 대피 commit이 증가하는 장면
- 정보 확인을 위해 접근했다가 문 너머 연기를 보고 회귀·경고하는 장면
- 익숙한 정문 혼잡 vs 잘 보이는 비상문
- 닫힌 비상문을 다른 NPC가 먼저 사용하는 장면
- 잠긴 문 정보가 주변 그룹에 전파되는 장면
- 문 너머 연기 확인 후 회귀
- 직원의 문 개방·유도와 군중 재분배
- 소화기 1개를 두 NPC가 동시에 선택
- 소화 중 퇴로 방화문 상태 변화
- 이동 제약 NPC와 일반 NPC의 문 통과
- 그룹 구성원이 문 양쪽으로 분리되는 상황
- 보호 대상의 위치를 아는 NPC만 제한적으로 재진입을 요청하는 장면
- 미commit 제3자의 관찰·촬영이 직접 위험과 공식 지시로 즉시 취소되는 장면
- 훈련·가시거리 조건에 따라 저자세/벽 추종 locomotion이 전환되는 장면
- 모든 출구 gateway가 막힌 상황

## 17. V&V와 보정 원칙

“사람처럼 보인다”는 시각적 인상만으로 검증하지 않는다.

NIST evacuation model V&V 분류에 맞춰 다음을 분리 측정한다.

- pre-evacuation time
- movement/navigation
- exit usage
- route availability
- flow constraints
- interaction completion/abort
- 행동 분포와 불확실성

관찰값과 프로젝트 제안값을 분리한다.

- 연구 관찰 범위: calibration target
- 현재 프로젝트 숫자: prior
- 물리·안전 제약: invariant
- 애니메이션 시간: asset parameter

각 기준 시나리오를 다회 실행해 평균뿐 아니라 분포, 꼬리, 실패율과 경로별 사용량을 비교한다. 보정에 사용한 시나리오와 최종 성능을 보고하는 hold-out 시나리오를 나눈다.

근거 강도는 source ID와 별도로 기록한다.

| 등급 | 의미 | 코드 사용 원칙 |
|---|---|---|
| E1 | 원자료가 해당 모집단·분모·행동 정의와 수치를 직접 제시 | 그대로 쓰더라도 적용 범위 메타데이터 필수 |
| E2 | 원자료 수치에서 조건부 비율·상대 순서 등을 계산 | 변환식과 분모 기록 |
| P | 프로젝트 prior·시연용 값·전문가 판단 | DataAsset 외부화, 민감도 분석 필수 |
| INV | 물리·안전·동시성 불변조건 | 행동 분포 보정을 위해 완화 금지 |

보정은 다음 순서로 수행한다.

1. 단위·통합 테스트로 INV 위반을 먼저 제거한다.
2. 한 번에 한 파라미터군만 바꿔 민감도를 측정한다.
3. `Calibration` 시나리오에서 pre-evacuation·행동 수·출구 사용을 맞춘다.
4. 다른 화재 위치·점유 분포·문 상태의 `Holdout` 시나리오에서 재검증한다.
5. 평균뿐 아니라 median, p90/p95, 실패율, censoring 비율과 bootstrap 신뢰구간을 보고한다.
6. 맞지 않는 결과를 숨기기 위해 생명 위험 threshold나 인터럽트를 조정하지 않는다.

## 18. 구현 단계와 예상 작업량

| 단계 | 작업 | 핵심 산출물 | 선행조건 | 예상 인일 |
|---|---|---|---|---:|
| 0 | 현재 상태 고정 | 구현/목표 표 정리, 기존 테스트 baseline, 작은 smoke test fixture | 없음 | 2~3 |
| 1 | 결정 기반 정리 | severity belief, evidence revision, RNG 분리, policy/scenario hash | 0 | 3~5 |
| 2 | cognition·14개 행동 | 기억·신뢰도·스트레스, masked task utility, 행동 매핑 | 1 | 5~7 |
| 3 | 범용 interaction | registry, affordance, proposal/resolver, executor | 1 | 3~4 |
| 4 | 문 | Actor, NavLink, queue/passage, 잠김·jam·방화문 | 3 | 4~6 |
| 5 | 소화기 | 장비 lifecycle, 발화원, 안전 gate, 중단 | 3·4 | 4~6 |
| 6 | 배치 | OccupancyZone, deterministic weighted rounding, 실내 품질 지표 | 0 | 2~4 |
| 7 | 표현·기록 | montage/IK/VFX/audio, event trace, timeline world state | 2·4·5 | 4~6 |
| 8 | V&V·보정 | 기능 테스트, 20/100/500 부하, Monte Carlo, holdout 보고 | 전체 | 5~8 |

코드 기준 총 32~49 인일을 계획값으로 둔다. 문·소화기 mesh/socket/montage/Niagara 제작, CAD 충돌 정리, CFD scenario branch 생성, 문헌 추가 조사와 사용자 검수 시간은 별도다. 여러 개발자가 병렬 작업하면 달력 기간은 줄어들 수 있지만 의존성과 통합 검증 인일은 사라지지 않는다.

## 19. 구현 우선순위

기능 추가 전 Gate 0에서 현재 `zion-fs`의 5개 자동화 테스트, Editor 빌드, 20 NPC 배치 결과를 baseline으로 고정한다. 작은 deterministic smoke fixture를 테스트에서 생성해 경보→연기→생명 위험 전이를 검증하고, 실제 대형 `smoke_data.bin` 부재 때문에 전체 구조의 merge가 영구히 막히지 않도록 한다. 대표 운영 시나리오의 실제 데이터 검수는 별도 release gate로 유지한다.

그다음 첫 구현은 다음 vertical slice로 시작한다.

```text
문 2개
  ├─ 정상 self-closing 비상문
  └─ 잠긴 일반문

소화기 1개
  └─ 범용 ABC 소화기

발화원 1개
  └─ A급 초기 화재

NPC 5명
  ├─ 직원 1
  ├─ 익숙한 사용자 2
  └─ 방문자 2
```

검증 장면:

1. 방문자는 처음에는 비상문을 모른다.
2. 직원이 비상문을 열고 알린다.
3. 일부 NPC의 문 knowledge와 route가 갱신된다.
4. 훈련된 직원만 조건부로 소화기를 예약한다.
5. 문 너머 위험 또는 퇴로 상실 시 소화를 중단한다.
6. 모든 NPC가 문 queue를 통해 충돌 없이 대피한다.

이 vertical slice가 통과한 뒤 20명·2개 층·다중 문으로 확대한다.

## 20. 완료 기준

### 코드·안전 완료

- Development Editor 빌드 성공
- 기존 `YUFS.NPC.Decision` 테스트 회귀 없음
- 기존 ONNX 28입력/11출력 계약 유지
- 문·소화기 신규 자동화 테스트 전부 성공
- 20 NPC 문 queue에서 capsule 겹침과 문 관통 0회
- 한 소화기 중복 소유 0회
- 잠긴 문 무한 재시도 0회
- 확인된 치명 경로 선택 0회
- 모든 생명 위험 인터럽트가 1 fixed tick 이내 처리
- 동일 policy/scenario·정렬 관측 이벤트·seed 조건에서 2회 실행의 canonical event trace hash 일치
- timeline seek 후 NPC·문·소화기·발화원 상태 일치
- 초기 배치가 OccupancyZone·capacity·실내 판정 불변조건을 모두 만족

### 행동 모델 검증 완료

- 각 정책 수치에 source ID와 E1/E2/P/INV 등급이 존재
- 문/출구 사용량, pre-evacuation time, 대피 전 행동 수가 사전 정의한 calibration interval 안에 포함
- calibration에 사용하지 않은 holdout 시나리오에서도 방향성과 신뢰구간이 허용 기준을 만족
- 14개 행동 중 정량 근거가 없는 항목은 정확한 비율을 재현한다고 주장하지 않음
- 현재 rule-based와 목표 policy의 차이를 scenario별 보고서로 남김
- ONNX는 입력 차원뿐 아니라 새 observation 분포와 출력 행동 결과를 재검증하기 전까지 대피 commit 권한을 갖지 않음

### 시각·운영 완료

- 시각 PIE에서 접근·정렬·조작·통과·진압·중단이 자연스럽게 확인됨
- smoke fixture로 CI 경로를 검증하고, 실제 `smoke_data.bin`이 있는 대표 시나리오에서 E2E 검수를 완료
- 로그가 중단돼도 마지막 정상 JSONL 행까지 분석 가능하고 `runCompleted`가 정확히 기록됨

## 21. 근거와 출처 연결

### 21.1 프로젝트 자료 source ID

| ID | 파일 | 사용한 부분 | 해석 제한 |
|---|---|---|---|
| A | `fire-evacuation-behavior-rates.html` | 14개 행동 범주, 관찰 범위, 원출처 연결 | 사건·실험·대리지표가 혼합되어 단일 인구 확률로 사용하지 않음 |
| B | `화재대피_NPC_행동비율_근거브리프.html` | 초기 행동 share, 경로 70/20/10, 지연 순서의 프로젝트 초안 | 연구값과 제안값 표시를 유지하며 대부분 P등급 |
| C | `3.txt` | 정보 탐색·고립·사회행동 등 누락 점검 | 분모와 출처가 없고 물음표 값이 있어 정량 입력으로 사용하지 않음 |
| D | `FRN-0953.pdf`, Wood (1972) | p.48 초기 행동, p.53~54 익숙한 출구·재진입, p.55 연기 진입·회귀 | 오래된 영국 화재 표본이므로 현대 대학 건물의 직접 확률로 일반화하지 않음 |
| E | `odpm_fire_033353.pdf`, Galea et al. (2004) | p.23~26 대피 개시·행동 수, p.47~50 물품·정보 탐색, p.68~73 그룹·리더·도움 | WTC 언론 계정의 사후 내용 분석 자료이므로 조건과 censoring을 함께 해석 |
| F | `s10694-026-01928-w.pdf`, Wong et al. (2026) | p.2~3 다층 영향 변수, p.9~11 외적 타당도·투명성·데이터 수집 방향 | 확률표가 아니라 모델 구조와 연구 과제의 근거 |
| G | `ZION_FS_CHANGE_GUIDE_KO_v2.md` | 현재 브랜치 구현·검증·미구현 항목 점검 | 행동 실측 근거가 아니라 내부 기술 검토 자료 |
| H | `fire-evacuation-npc-probability-state-spec.html` | A~F를 상태·확률·인터럽트·경로·로그로 통합한 기존 설계 | 원자료가 아니라 통합 해석본이므로 수치는 원자료까지 역추적 |

검토에 사용한 로컬 원자료의 SHA-256은 다음과 같다. 팀 저장소에는 저작권과 용량 정책에 맞는 별도 위치에 원문을 보관하고, 이 해시로 동일 파일인지 확인한다.

| ID | bytes | SHA-256 |
|---|---:|---|
| A | 59,441 | `5D480AC0B1C16C94D9C23F377E6032E5EFD6806379A2DED42611AEDC33BF5DAB` |
| B | 38,309 | `3C52F9A13CFA294FB531BA3CC887CFB173B8E72B1BF402CFB90083783D6F7783` |
| C | 1,671 | `F064C8001D3AADF8EE8C2FC9EEFA36393C9B1F7A5659816D95D6F8C37452B008` |
| D | 2,960,787 | `57EA03C6D656494D5510A596FC5BBD9F572FA7193334E7D9D24BB1AC8AA4FDF9` |
| E | 1,957,810 | `8F9BF1E8D770A7C0A549050179C6769559FFA88EEAA080D709CBD1C9C8148E39` |
| F | 925,113 | `3D631327DE3FA109632DC35C069D3396E96F16409E0225CD3C362D7E23159CF9` |
| G | 35,581 | `900BD3E51C6EB910BD5EB879B0E67793554DC13A8F7F9040D4EA3B20BD7AAF1D` |
| H | 79,148 | `B9A869CDDB0FC6EE84B7417AA09E301938E0EDACA8C206DC40BA9C364C41BDEA` |

### 21.2 구현 항목별 사용처

| 구현 항목 | 주 출처 | 반영 방식 |
|---|---|---|
| 14개 행동의 존재와 분류 | A, C, D, E | 동일 레벨 선택지가 아니라 상태·태스크·보정·인터럽트·이동 방식으로 재분류 |
| 대피 전 행동 수 | E p.25~26 | 1~5/6~9/10~15회 분포를 결과 보정 목표로 사용; NPC별 목표 횟수로 강제하지 않음 |
| 정보 탐색 | E p.48~50 | 창문·인접공간·방송·동료 후보와 조건부 비율을 보정에 사용 |
| 물품 회수 | E p.47~48 | 근거리 회수와 먼 위치 역방향 이동을 분리 |
| 그룹·리더·도움 | E p.68~73, F | 관계·역할·정보 전달·예약 구조에 반영 |
| 익숙한 출구·재진입·연기 진입 | D p.53~55 | 경로 utility와 상태 존재 근거; 현대 일반확률로 직접 사용하지 않음 |
| 초기 소화 | A, D p.48, USFA, OSHA | 행동 존재는 관찰자료, 활성 여부는 훈련·화재 규모·약제·퇴로 안전 gate로 결정 |
| belief prior와 태스크 weight | B, G | P등급 초기 정책값; DataAsset·민감도·holdout 보정 대상 |
| 인간 행동 처리 순환 | NIST TN 1632, F | 지각→해석→결정→행동→결과 지각 구조 |
| V&V | NIST TN 1822, F | pre-evacuation·경로·흐름·상호작용을 분리 검증 |
| 문·방화문 | GOV.UK guidance, 프로젝트 물리 요구 | self-close·통과·방연 역할과 behavior-only/CFD branch 분리 |
| 현재 코드 경계 | G와 `zion-fs` 소스 | 구현 완료와 목표 설계를 분리하고 테스트로 재확인 |

### 21.3 외부 참고

외부 자료는 안전 규칙과 모델 구조를 보강한다. 프로젝트 A~F의 수치와 혼합할 때는 해당 모집단과 측정 정의가 같은지 먼저 확인한다.

| 근거 | 반영한 부분 |
|---|---|
| NIST TN 1632 | 지각 → 해석 → 결정 → 행동의 반복 과정 |
| NIST 인지 편향 연구 | 제한된 정보·시간·인지 자원과 휴리스틱 |
| NIST TN 1822 | pre-evacuation, navigation, exit usage, route availability, flow constraints V&V |
| NIST evacuation modelling review | 친숙한 경로, 사회적 영향, 조건부 경로 변경 |
| USFA 소화기 체크리스트 | 작은 화재, 연기 안전, 명확한 퇴로, 경보/신고 |
| OSHA 29 CFR 1910.157 | 초기 단계 화재, 사용자 교육, 적절한 소화기와 유지 상태 |
| GOV.UK fire door guidance | 방화문 self-close와 방연 역할 |

- [NIST — The Process of Human Behavior in Fires, TN 1632](https://www.nist.gov/publications/process-human-behavior-fires-0)
- [NIST — Cognitive Biases Within Decision Making During Fire Evacuations](https://www.nist.gov/publications/cognitive-biases-within-decision-making-during-fire-evacuations)
- [NIST — Verification and Validation of Building Fire Evacuation Models, TN 1822](https://www.nist.gov/publications/process-verification-and-validation-building-fire-evacuation-models)
- [NIST — A Multi-disciplinary Perspective on Human Behavior in Evacuation Models](https://www.nist.gov/publications/multi-disciplinary-perspective-representing-human-behavior-evacuation-models)
- [U.S. Fire Administration — Choosing and Using Fire Extinguishers](https://www.usfa.fema.gov/prevention/home-fires/prepare-for-fire/fire-extinguishers)
- [OSHA — Portable Fire Extinguishers, 29 CFR 1910.157](https://www.osha.gov/laws-regs/regulations/standardnumber/1910/1910.157)
- [GOV.UK — Fire door guidance](https://www.gov.uk/government/publications/fire-safety-england-regulations-2022-fire-door-guidance/fire-safety-england-regulations-2022-fire-door-guidance)

## 22. 최종 범위 구분

### 포함

- 범용 NPC–객체 상호작용 프레임워크
- 불완전 지각, 기억, 정보 신뢰도, 스트레스와 행동 관성
- 문 열기·닫기·잡아주기·통과·잠김·jam·대기열
- 방화문 self-close와 NavLink 연동
- 소화기 획득·운반·조준·방사·진압·중단
- 객체 경쟁의 deterministic 해결
- 사회적 정보 전달과 그룹 영향
- trace/timeline/test/calibration 구조

### 제외

- 범용 인간 수준 인공지능 또는 자연어 추론
- NPC별 대형 언어 모델 호출
- 런타임 CFD 재계산
- 모든 문 조합의 화재 확산 계산
- 소방대 수준 화재 진압
- 복잡한 군중 압력에 의한 실제 부상 물리
- 문 파손·창문 파괴의 정밀 구조 해석
- 소화기 재충전·정비 시스템

NPC의 현실성은 더 많은 랜덤 행동을 넣어서 만들지 않는다. **모르는 것은 모르게 하고, 본 것과 들은 것을 기억하며, 행동에는 시간이 걸리고, 새 단서가 생기면 계획을 바꾸고, 다른 사람과 물리적 객체의 제약을 함께 받게 하는 것**을 핵심으로 한다.
