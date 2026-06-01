# YUFS 아키텍처 다이어그램

> UE 5.7 기반 화재 대피 시뮬레이션 — Kuligowski PADM 행동심리 모델 + MLP 정책 (ONNX)

---

## 1. 클래스 다이어그램 — 전체 시스템 개요

```mermaid
classDiagram
    direction TB

    class AYUFSSimulationController {
        <<AActor>>
        +CurrentPhase : ESimPhase
        +TotalRunCount : int32
        +FireStartDelaySeconds : float
        +StartSimulation()
        +PauseSimulation()
        +ResumeSimulation()
        +RegisterNPC(NPC)
        +IsNPCSimulationEnabled() bool
        -TickFireActivePhase(DeltaTime)
        -FinalizeRun()
        -UpdateLiveCounts()
    }

    class AYUFSEvacuationNPC {
        <<ACharacter>>
        -MLPolicy : FYUFSOnnxPolicy
        -CurrentAction : EYUFSAction
        -bDataCollectionMode : bool
        -bLogTransitions : bool
        +Tick(DeltaTime)
        +NotifyEpisodeFinished(TerminalReason)
        +DriveMovementToward(Target)
        +OnCommReceived(CommType, ...)
        -BuildObservation(Out)
        -TickPolicy(DeltaTime)
        -ExecuteCurrentAction(DeltaTime)
        -FlushLearningTransition(NextObs, reason)
    }

    class AYUFSEmergencyCommSystem {
        <<AActor>>
        +OnEmergencyComm : Delegate
        +AlarmRadius : float
        +AnnouncementRadius : float
        +StaffGuidanceRadius : float
        +ActivateAlarm()
        +BroadcastPreRecordedMessage(Message)
        +BroadcastLiveAnnouncement(Message)
        +DispatchStaffGuidance(TargetExit)
    }

    class AYUFSBinaryManager {
        <<AActor>>
        +GetCurrentFrame() int32
        +GetSmokeDensityAtLocation(Loc, Frame, OutDensity) bool
        +GetTemperatureAtLocation(Loc, Frame, OutTemp) bool
        -LoadDynamicChunkAsync(Start, End, Gen)
    }

    class AYUFSLevelDataManager {
        <<AActor>>
        -CachedExits : TArray~AYUFSExitPoint~
        -CachedShelters : TArray~AYUFSShelterPoint~
        +GetNearestSafeExit(From, bSmokeFreeOnly, Frame) FVector
        +GetFamiliarExit(NPCSpawnLocation) FVector
        +GetNearestAvailableShelter(From) FVector
        +IsLocationDangerous(Location, Frame) bool
        +GetPathDangerScore(Path, Frame) float
    }

    class AYUFSHeterogeneousVolume {
        <<AActor>>
        +StartFire()
        +PauseFire()
        +ResumeFire()
        +ResetFire()
    }

    class AYUFSExitPoint {
        <<AActor>>
        +bIsActive : bool
    }

    class AYUFSShelterPoint {
        <<AActor>>
        +Capacity : int32
    }

    AYUFSSimulationController "1" --> "N" AYUFSEvacuationNPC : manages/RegisteredNPCs
    AYUFSSimulationController --> AYUFSEmergencyCommSystem : triggers comms
    AYUFSSimulationController --> AYUFSBinaryManager : reads frame
    AYUFSSimulationController --> AYUFSLevelDataManager : queries exits
    AYUFSSimulationController --> AYUFSHeterogeneousVolume : controls fire
    AYUFSEvacuationNPC --> AYUFSEmergencyCommSystem : subscribes OnEmergencyComm
    AYUFSEvacuationNPC --> AYUFSBinaryManager : reads smoke data
    AYUFSEvacuationNPC --> AYUFSLevelDataManager : queries exits/shelters
    AYUFSLevelDataManager "1" o-- "N" AYUFSExitPoint : collects
    AYUFSLevelDataManager "1" o-- "N" AYUFSShelterPoint : collects
    AYUFSBinaryManager --> AYUFSHeterogeneousVolume : ref
```

---

## 2. 클래스 다이어그램 — NPC 서브시스템 상세

```mermaid
classDiagram
    direction TB

    class AYUFSEvacuationNPC {
        <<ACharacter>>
        -PerceptionComp : UYUFSNPCPerceptionComponent
        -BehaviorSM : UYUFSBehaviorStateMachine
        -Navigator : UYUFSSmokeAwareNavigator
        -SocialComp : UYUFSSocialInfluenceComponent
        -DebugComp : UYUFSNPCDebugComponent
        -MLPolicy : FYUFSOnnxPolicy
        -CurrentAction : EYUFSAction
        -PolicyTickInterval : float = 0.1
        -MinActionHoldDuration : float = 2.0
        +Tick(DeltaTime)
        +NotifyEpisodeFinished(reason)
        +DriveMovementToward(Target)
        -BuildObservation(Out FYUFSNPCObservation)
        -TickPolicy(DeltaTime)
        -ExecuteCurrentAction(DeltaTime)
        -ResolveNavigationTarget(Action) FVector
        -FlushLearningTransition(NextObs, reason)
        -UpdateStuckDetection(DeltaTime)
    }

    class IYUFSDecisionPolicy {
        <<interface>>
        +SelectAction(Obs) EYUFSAction
        +OnTransition(PrevObs, Action, NextObs, Reward, bDone)
        +IsDataCollectionMode() bool
        +LoadModel(Path)
    }

    class FYUFSOnnxPolicy {
        -FallbackPolicy : FYUFSRuleBasedPolicy
        -ModelPath : FString
        -RuntimeName : FString
        -bDataCollectionMode : bool
        -bModelReady : bool
        -CpuModelInstance
        -GpuModelInstance
        -InputBuffer : TArray~float~
        -OutputBuffer : TArray~float~
        +SelectAction(Obs) EYUFSAction
        +SetDataCollectionMode(bEnable)
        -EnsureModelLoaded() bool
        -ConfigureCpuModelInstance() bool
        -ConfigureGpuModelInstance() bool
        -RunCpuInference(StateVec, OutLogits) bool
        -RunGpuInference(StateVec, OutLogits) bool
        -ParseInputShape(InputDescs, OutputDescs, OutShape) bool
        -FinalizeBuffers(OutputShapes, OutputDesc, InputShape) bool
        -PrepareInferenceBindings(StateVec, InBindings, OutBindings) bool
        -FindLatestOnnxModelPath() FString
        -SelectActionFromLogits(Logits) EYUFSAction
    }

    class FYUFSRuleBasedPolicy {
        +SelectAction(Obs) EYUFSAction
    }

    class FYUFSPolicyFactory {
        <<static utility>>
        +Create(EPolicyType, ModelPath, RuntimeName) TSharedPtr~IYUFSDecisionPolicy~
    }

    class UYUFSBehaviorStateMachine {
        <<UActorComponent>>
        +Config : UYUFSBehaviorConfig
        -CurrentState : EYUFSBehaviorState
        -RiskPerception : float
        -SmokeExposureAccumulated : float
        -StateTimer : float
        +TickStateMachine(DeltaTime, Obs)
        +GetCurrentState() EYUFSBehaviorState
        +GetRiskPerception() float
        +GetSmokeExposure() float
        +IsIncapacitated() bool
        +IsCrawling() bool
        +OnAlarmReceived()
        +OnPreRecordedMessageReceived()
        +OnLiveAnnouncementReceived()
        +OnStaffGuidanceReceived()
        -TryTransition(Obs)
        -AccumulateRiskPerception(Obs, DeltaTime)
        -AccumulateSmokeExposure(Obs, DeltaTime)
        -CheckEmergencyOverride(Obs) bool
    }

    class UYUFSNPCPerceptionComponent {
        <<UActorComponent>>
        +Config : UYUFSPerceptionConfig
        -BinaryManager : AYUFSBinaryManager
        -CachedSmokeDensity : float
        -CachedTemperature : float
        -CachedRiskLevel : float
        +UpdatePerception(Frame)
        +GetSmokeDensity() float
        +GetTemperature() float
        +GetSmokeInFrontNormalized() float
        +GetSmokeAboveNormalized() float
        +GetRiskLevel() float
    }

    class UYUFSSmokeAwareNavigator {
        <<UActorComponent>>
        +RerouteCheckInterval : float
        +SmokeBlockThreshold : float
        -CurrentPath : TArray~FVector~
        -CurrentDestination : FVector
        +RequestPathAsync(Dest, Frame)
        +OnPathFound(PathId, Result, NavPath)
        +GetSteeringTarget(ActorLocation, LookAhead) FVector
        +UpdateWaypoint(ActorLocation, Radius)
        +CheckAndReroute(Frame)
        +ClearPath()
        +GetCurrentDestination() FVector
    }

    class UYUFSSocialInfluenceComponent {
        <<UActorComponent>>
        +SocialInfluenceRadius : float
        +BystanderEffectStrength : float
        +SocialDelayPerMember : float
        -NearbyNPCs : TArray~ACharacter~
        +UpdateSocialContext()
        +GetNearbyEvacuatingRatio() float
        +GetNearbyNPCCount() int32
        +GetAverageEvacuationDestination() FVector
        +GetNearestNPCNeedingHelpLocation() FVector
        +ShouldHelpNearbyNPC() bool
        +GetGroupSpeedMultiplier() float
    }

    class FYUFSNPCObservation {
        <<USTRUCT 29-dim>>
        +SmokeDensityAtSelf : float
        +TemperatureAtSelf : float
        +SmokeInFrontNormalized : float
        +SmokeAboveNormalized : float
        +RiskLevel : float
        +SimTimeNormalized : float
        +DistToNearestExit : float
        +DistToFamiliarExit : float
        +DirToNearestExit : FVector
        +bNearestExitSmokeFree : bool
        +DistToNearestShelter : float
        +NearbyEvacuatingRatio : float
        +NearbyNPCCount : int32
        +bAlarmSounding : bool
        +bReceivedStaffGuidance : bool
        +CurrentState : EYUFSBehaviorState
        +RiskPerception : float
        +StressLevel : float
        +SmokeExposureAccumulated : float
        +ToFloatArray() TArray~float~
    }

    class YUFSRewardCalculator {
        <<static utility>>
        +Calculate(PrevObs, Action, NextObs, TerminalReason) float
    }

    class FYUFSExperienceLogger {
        <<static utility>>
        +LogTransition(AgentId, RunIndex, StepIndex, SimFrame, SimTime, State, Action, Reward, NextState, bDone, reason)
        +GetLogFilePath() FString
    }

    IYUFSDecisionPolicy <|.. FYUFSOnnxPolicy : implements
    IYUFSDecisionPolicy <|.. FYUFSRuleBasedPolicy : implements
    FYUFSOnnxPolicy *-- FYUFSRuleBasedPolicy : fallback
    FYUFSPolicyFactory ..> IYUFSDecisionPolicy : creates

    AYUFSEvacuationNPC *-- UYUFSBehaviorStateMachine : component
    AYUFSEvacuationNPC *-- UYUFSNPCPerceptionComponent : component
    AYUFSEvacuationNPC *-- UYUFSSmokeAwareNavigator : component
    AYUFSEvacuationNPC *-- UYUFSSocialInfluenceComponent : component
    AYUFSEvacuationNPC *-- FYUFSOnnxPolicy : policy (value)
    AYUFSEvacuationNPC ..> FYUFSNPCObservation : builds each tick
    AYUFSEvacuationNPC ..> YUFSRewardCalculator : calculates reward
    AYUFSEvacuationNPC ..> FYUFSExperienceLogger : writes transitions

    UYUFSBehaviorStateMachine ..> FYUFSNPCObservation : reads
    FYUFSOnnxPolicy ..> FYUFSNPCObservation : reads
```

---

## 3. 열거형 & 데이터 구조

```mermaid
classDiagram
    class EYUFSBehaviorState {
        <<enumeration PADM 기반>>
        Normal
        Perceiving
        Milling
        RiskAssessment
        Preparing
        Evacuating
        Helping
        Sheltering
        Crawling
        Incapacitated
    }

    class EYUFSAction {
        <<enumeration 12개 행동>>
        Idle
        SeekInformation
        AlertNearbyOccupants
        GatherBelongings
        EvacuateToNearestExit
        EvacuateToFamiliarExit
        HelpOther
        MoveToShelter
        WaitForInfo
        Cough
        FollowCrowd
        Film
    }

    class EYUFSTerminalReason {
        <<enumeration>>
        None
        ReachedExit
        Incapacitated
        TimedOut
    }

    class ESimPhase {
        <<enumeration>>
        WaitingToStart
        FireStartDelay
        FireActive
        Completed
    }

    class EYUFSCommType {
        <<enumeration van der Wal 4종>>
        AlarmOnly
        PreRecordedMessage
        LiveAnnouncement
        StaffGuidance
    }

    class FSimRunResult {
        <<USTRUCT>>
        +RunIndex : int32
        +TotalNPCCount : int32
        +EvacuatedCount : int32
        +IncapacitatedCount : int32
        +EvacuationRate : float
        +SimDurationSeconds : float
        +AverageEvacuationTime : float
    }
```

---

## 4. 시퀀스 다이어그램 — NPC 매 Tick 사이클

```mermaid
sequenceDiagram
    autonumber
    participant SC  as SimulationController
    participant NPC as EvacuationNPC
    participant PC  as PerceptionComp
    participant SOC as SocialComp
    participant SM  as BehaviorStateMachine
    participant POL as FYUFSOnnxPolicy
    participant NAV as SmokeAwareNavigator
    participant LOG as ExperienceLogger

    SC->>NPC: Tick(DeltaTime)

    NPC->>SC: IsNPCSimulationEnabled()?
    alt 일시정지 또는 FireActive 아님
        NPC->>NAV: ClearPath()
        NPC->>NPC: StopMovementImmediately()
        NPC-->>SC: return (조기 종료)
    end

    NPC->>PC: UpdatePerception(CurrentFrame)
    Note over PC: BinaryManager로부터 연기·온도 샘플링
    PC-->>NPC: SmokeDensity, Temperature, RiskLevel 캐시 갱신

    NPC->>SOC: UpdateSocialContext()
    Note over SOC: 반경 내 ACharacter 스캔
    SOC-->>NPC: NearbyNPCs, EvacuatingRatio, GroupSpeed 갱신

    NPC->>SM: TickStateMachine(DeltaTime, Obs)
    SM->>SM: AccumulateRiskPerception(Obs, DeltaTime)
    SM->>SM: AccumulateSmokeExposure(Obs, DeltaTime)
    SM->>SM: TryTransition(Obs)
    Note over SM: Incapacitated > Crawling > EmergencyOverride ><br/>Normal→Perceiving→Milling→RiskAssessment→<br/>Preparing→Evacuating→Helping/Sheltering
    SM-->>NPC: EYUFSBehaviorState 업데이트

    NPC->>NPC: TickPolicy(DeltaTime)
    NPC->>NPC: BuildObservation(Obs) → FYUFSNPCObservation
    NPC->>POL: SelectAction(Obs)

    alt ONNX 모델 로드 성공 & 학습 모드 OFF
        POL->>POL: Obs.ToFloatArray() → 29-dim StateVec
        POL->>POL: RunCpuInference(StateVec) → Logits[12]
        POL->>POL: argmax(Logits) → BestIndex
        POL-->>NPC: EYUFSAction (MLP 출력)
    else 모델 없음 또는 bDataCollectionMode
        POL->>POL: FallbackPolicy.SelectAction(Obs)
        Note over POL: PADM 규칙: StaffGuidance > Incapacitated ><br/>Perceiving→SeekInfo, Milling→Film/Alert,<br/>Evacuating→FollowCrowd/FamiliarExit/NearestExit
        POL-->>NPC: EYUFSAction (규칙 기반)
    end

    NPC->>NPC: ActionHoldTimer 경과 또는 상태 변경 시 OnActionChanged(NewAction)
    NPC->>NPC: ExecuteCurrentAction(DeltaTime)

    alt 이동 액션 (Evacuate, Shelter, Help, FollowCrowd)
        NPC->>NAV: CheckAndReroute(Frame)
        Note over NAV: 경로상 연기 농도 재평가 → 필요 시 재탐색
        alt 목적지 갱신 필요
            NPC->>NAV: ClearPath() → RequestPathAsync(Target, Frame)
        end
        NPC->>NPC: DriveMovementToward(Target)
        NPC->>NAV: UpdateWaypoint(ActorLocation, 80cm)
        NPC->>NAV: GetSteeringTarget(ActorLocation, 120cm)
        NAV-->>NPC: SteeringTarget
        NPC->>NPC: AddMovementInput(Dir * GroupSpeedMult)
    else 비이동 액션 (SeekInfo, Alert)
        NPC->>NPC: SetActorRotation(LookAnchorYaw + sin(t) * Amplitude)
    else 애니메이션 액션 (Cough, Film)
        NPC->>NPC: PlayAnimMontage(CoughMontage / FilmMontage)
    else Idle / WaitForInfo / GatherBelongings
        Note over NPC: 이동 없음
    end

    NPC->>NPC: BuildObservation(CurrentObs)
    NPC->>LOG: FlushLearningTransition(CurrentObs, TerminalReason)
    Note over LOG: Saved/RLTransitions/*.csv 에<br/>state, action, reward, next_state, done 한 행 기록
    NPC->>NPC: PrevObservation = CurrentObs
```

---

## 5. 시퀀스 다이어그램 — 긴급 통신 이벤트 흐름

```mermaid
sequenceDiagram
    autonumber
    participant SC   as SimulationController
    participant LDM  as LevelDataManager
    participant COMM as EmergencyCommSystem
    participant NPC1 as EvacuationNPC (범위 내)
    participant NPC2 as EvacuationNPC (범위 외)
    participant SM1  as BehaviorSM (NPC1)

    Note over SC: FireActive 단계 — TickFireActivePhase()

    rect rgb(255, 240, 200)
        Note over SC,COMM: ① AlarmTriggerOffsetSeconds 경과 → 알람 발령
        SC->>COMM: ActivateAlarm()
        COMM->>COMM: OnEmergencyComm.Broadcast(AlarmOnly, Location, AlarmRadius, Zero)
        COMM-->>NPC1: OnCommReceived(AlarmOnly, ...)
        COMM-->>NPC2: OnCommReceived(AlarmOnly, ...)
        NPC1->>NPC1: Dist(self, Location) ≤ AlarmRadius → true
        NPC1->>NPC1: bAlarmSounding = true
        NPC1->>SM1: OnAlarmReceived()
        SM1->>SM1: RiskPerception += 0.1
        NPC2->>NPC2: Dist(self, Location) > AlarmRadius → 무시
    end

    rect rgb(200, 240, 255)
        Note over SC,COMM: ② PreRecordedMsgOffsetSeconds 경과 → 사전 녹음 방송
        SC->>COMM: BroadcastPreRecordedMessage(Message)
        COMM->>COMM: OnEmergencyComm.Broadcast(PreRecordedMessage, ..., AnnouncementRadius, ...)
        COMM-->>NPC1: OnCommReceived(PreRecordedMessage, ...)
        NPC1->>NPC1: bReceivedPreRecordedMsg = true
        NPC1->>SM1: OnPreRecordedMessageReceived()
        SM1->>SM1: RiskPerception += 0.2
        Note over SM1: 알람(+0.1)보다 높음 — 명확한 정보 제공
    end

    rect rgb(200, 255, 200)
        Note over SC,COMM: ③ StaffGuidanceOffsetSeconds 경과 → 스태프 직접 안내 (OR 0.33 최효과)
        SC->>LDM: GetNearestSafeExit(CommSystem.Location, true, CurrentFrame)
        LDM-->>SC: SafeExitLocation (연기 없는 최근접 출구)
        SC->>COMM: DispatchStaffGuidance(SafeExitLocation)
        COMM->>COMM: OnEmergencyComm.Broadcast(StaffGuidance, ..., StaffGuidanceRadius=800cm, SafeExit)
        COMM-->>NPC1: OnCommReceived(StaffGuidance, ...)
        NPC1->>NPC1: bReceivedStaffGuidance = true
        NPC1->>NPC1: StaffGuidedExitLocation = SafeExitLocation
        NPC1->>SM1: OnStaffGuidanceReceived()
        SM1->>SM1: RiskPerception += 0.5 (가장 큰 증가)
        Note over SM1: 다음 TickStateMachine에서<br/>Milling/RiskAssessment → Evacuating 즉시 전이
    end
```

---

## 6. 시퀀스 다이어그램 — 에피소드 종료 & ML 데이터 로깅

```mermaid
sequenceDiagram
    autonumber
    participant SC  as SimulationController
    participant LDM as LevelDataManager
    participant BIN as BinaryManager
    participant NPC as EvacuationNPC
    participant RWD as YUFSRewardCalculator
    participant LOG as ExperienceLogger
    participant GI  as YUFSGameInstance

    loop 매 Tick: UpdateLiveCounts()
        SC->>BIN: GetCurrentFrame()
        BIN-->>SC: CurrentFrame
        SC->>LDM: GetNearestSafeExit(NPC.Location, false, Frame)
        LDM-->>SC: NearestExit

        alt DistToExit < EvacuationSuccessDistanceCm (150cm)
            Note over SC,NPC: ── 대피 성공 경로 ──
            SC->>NPC: NotifyEpisodeFinished(ReachedExit)
            NPC->>NPC: BuildObservation(TerminalObs)
            NPC->>RWD: Calculate(PrevObs, LastAction, TerminalObs, ReachedExit)
            Note over RWD: +10 (출구 도달) + progress bonus<br/>- 0.01 (생존 비용) - 연기 패널티
            RWD-->>NPC: Reward
            NPC->>LOG: LogTransition(AgentId, RunIdx, Step, Frame, Time,<br/>PrevObs, Action, Reward, TerminalObs, bDone=true, ReachedExit)
            Note over LOG: Saved/RLTransitions/transitions_YYYYMMDD.csv<br/>마지막 행 기록 → Python train_rl로 학습 가능
            SC->>SC: LiveEvacuatedCount++
            SC->>SC: RegisteredNPCs.Remove(NPC)
            SC->>NPC: Destroy()

        else BehaviorSM.IsIncapacitated()
            Note over SC,NPC: ── 행동불능 경로 ──
            SC->>NPC: NotifyEpisodeFinished(Incapacitated)
            NPC->>RWD: Calculate(PrevObs, LastAction, TerminalObs, Incapacitated)
            Note over RWD: -10 (행동불능) - 연기·온도·누적노출 패널티
            RWD-->>NPC: Reward (음수)
            NPC->>LOG: LogTransition(..., bDone=true, Incapacitated)
            SC->>SC: LiveIncapacitatedCount++
            SC->>NPC: SetActorHiddenInGame(true)
            SC->>NPC: SetActorTickEnabled(false)
        end
    end

    alt RegisteredNPCs.IsEmpty() OR MaxSimDuration 초과
        Note over SC: ── 회차(Run) 종료 ──
        SC->>SC: FinalizeRun()
        SC->>SC: 잔여 NPC: NotifyEpisodeFinished(TimedOut)
        SC->>SC: FSimRunResult 집계
        SC->>SC: OnRunCompleted.Broadcast(Result)

        alt CurrentRunIndex < TotalRunCount (배치 실험 계속)
            SC->>GI: SetupNextRun(NextIndex, TotalRuns, AccumulatedResults)
            Note over GI: 레벨 리로드 후에도 회차 상태 보존
            SC->>SC: OpenLevel() → 전체 씬 리로드
            Note over GI,SC: BeginPlay에서 GI.bHasPendingBatchRun 확인<br/>→ CurrentRunIndex 복원 후 FireStartDelay 재시작
        else 모든 회차 완료
            SC->>SC: SetPhase(Completed)
            SC->>SC: HeterogeneousVolume.PauseFire()
        end
    end
```

---

## 7. PADM 행동 상태 전이 다이어그램

```mermaid
stateDiagram-v2
    [*] --> Normal : 시뮬레이션 시작

    Normal --> Perceiving : 연기·온도·알람·방송 감지

    Perceiving --> Milling : PerceivinDuration 경과

    Milling --> RiskAssessment : RiskPerception > Threshold 또는 StaffGuidance 수신
    Milling --> Preparing : MaxMillingDuration 초과

    RiskAssessment --> Preparing : RiskPerception > Threshold 또는 알람 울림
    RiskAssessment --> Milling : RiskPerception 낮아짐, 알람 없음

    Preparing --> Evacuating : PreparationDuration 경과 또는 StaffGuidance 수신

    Evacuating --> Helping : 주변 NPC 도움 필요, RiskPerception 낮음
    Evacuating --> Sheltering : 전방 경로 연기 차단, 현재 위치 연기 없음
    Evacuating --> Crawling : SmokeExposure >= CrawlThreshold

    Helping --> Evacuating : NPC 도움 불필요 또는 MaxHelpingDuration 초과

    Sheltering --> Evacuating : RiskPerception 낮아짐, SmokeDensity 해소

    Crawling --> Evacuating : SmokeExposure < CrawlThreshold (회복)
    Crawling --> Incapacitated : SmokeExposure >= IncapacitationThreshold

    note right of Evacuating
        긴급 오버라이드 (CheckEmergencyOverride)
        SmokeDensity > SmokeAwarenessThreshold x 2.0 시
        Crawling·Incapacitated 제외 모든 상태에서
        Evacuating으로 즉시 강제 전이
    end note

    Incapacitated --> [*] : 시뮬레이션에서 제거
```

---

## 8. ML 학습 파이프라인 개요

```mermaid
flowchart LR
    subgraph UE5["UE5 시뮬레이션 런타임"]
        SIM["AYUFSSimulationController\n(다회차 배치 실험)"]
        NPC["AYUFSEvacuationNPC\n(bLogTransitions = true)"]
        LOG["FYUFSExperienceLogger"]
        CSV["Saved/RLTransitions/\ntransitions_YYYYMMDD.csv"]
    end

    subgraph PY["Python train_rl/"]
        DS["dataset.py\nCSV 파싱·정규화"]
        BC["bc_model.py\nMLP (29→128→128→12)\nBehavior Cloning"]
        RF["train_rf.py\nRandomForest (200 trees)"]
        ONNX_BC["export_onnx.py\nBC → ONNX\n(정규화 내장)"]
        ONNX_RF["export_onnx_rf.py\nRF → ONNX\n(class expand)"]
        MODEL["Saved/RLModels/\n*.onnx"]
    end

    subgraph INFER["UE5 추론 (FYUFSOnnxPolicy)"]
        NNE["NNERuntimeORTCpu\n또는 NNERuntimeORTDml"]
        POL["ONNX 추론\n→ Logits[12]\n→ argmax → EYUFSAction"]
    end

    SIM -->|배치 실험 회차 진행| NPC
    NPC -->|FlushLearningTransition| LOG
    LOG -->|기록| CSV
    CSV -->|로드| DS
    DS -->|TensorDataset| BC
    DS -->|numpy array| RF
    BC -->|학습 완료| ONNX_BC
    RF -->|학습 완료| ONNX_RF
    ONNX_BC -->|저장| MODEL
    ONNX_RF -->|저장| MODEL
    MODEL -->|자동 탐색 FindLatestOnnxModelPath| NNE
    NNE --> POL
    POL -->|EYUFSAction| NPC
```
