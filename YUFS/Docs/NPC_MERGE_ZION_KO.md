# NPC_merge + zion-fs 통합 기록

## 기준

- 통합 브랜치: `codex/npc-merge-zion`
- 기준 부모: `NPC_merge@872d50a` (JJW 경로 탐색과 CYW 일상 행동)
- 추가 부모: `zion-fs@086fe2b` (문, 소화기, 도움, 행동 애니메이션과 통합 어댑터)
- `zion-fs`의 2026-09-14 19:46 병합은 `JJW_NPC_BEHAVIOR@204a8d3`을 가져온 것이다. `NPC_cyw` 병합이 아니다.
- main, JJW_NPC_BEHAVIOR, NPC_merge, zion-fs 원격 브랜치는 이 작업에서 변경하지 않는다.

## 충돌 해결

충돌은 `YUFSEvacuationNPC.cpp`의 Tick 처리에 발생했다. 어느 한쪽 파일로 덮어쓰지 않고 다음 순서로 연결했다.

1. 타임라인 관찰과 일시정지는 산책을 중단하고 기존 상호작용·이동 정지 처리를 유지한다.
2. 시작 대기와 화재 시작 지연에는 CYW의 일상 행동을 유지한다. 명시적으로 요청한 상호작용 시연 모드는 별도 실행한다.
3. 화재 활성화 후에는 JJW 지각·상태머신과 zion의 보조 인지·상호작용 관측을 갱신한다.
4. 아직 Normal 상태인 NPC는 산책을 계속한다. 단, 다른 NPC에게 직접 도움을 받는 중에는 접촉 상호작용이 이동을 제어한다.
5. Normal 상태를 벗어나면 산책 AI 이동을 중단하고 JJW 대피 정책 및 zion 상호작용 실행으로 넘긴다.
6. 산책 시 네이티브 애니메이션은 기존의 호환 걷기 시퀀스를 재사용한다. 실제 행동 enum을 HelpOther로 바꾸지는 않는다.
7. 외부 내비게이션 드라이버가 켜진 NPC에는 네이티브 산책 AI가 이동 명령을 보내지 않는다.

## 보존한 내용

- NPC_merge의 Prototype 맵, 기존 화재·NPC·UI 에셋.
- JJW 행동 상태머신·설정, 개인별 위험 기억, 기존 지각·내비게이션 테스트 16개.
- CYW의 산책 설정과 대피/기어가기 상태에서만 출구 도착을 성공으로 집계하는 조건.
- zion의 소화 허용 조건, 위험 상승 시 중단, 데이터 미확인 시 접근 금지, 문 충돌·예약, 도움 후 대피 연속성.
- 기존 28개 모델 입력과 11개 행동 계약.

## 추가 회귀 테스트

`YUFS.NPC.Integration.Everyday` 그룹의 테스트 4개를 추가했다.

- `AlarmHandsOffToJJW`: 실제 경보 관측 후 산책을 중단하고 JJW 정보 탐색으로 전환.
- `PauseResumeAndReplay`: 화재 전 산책, 일시정지·재개, 타임라인 이동 차단.
- `RespectsExternalNavigation`: 외부 이동 드라이버와 네이티브 산책의 중복 제어 방지.
- `WalkingAnimationKeepsIdlePolicy`: 실제 RL 메시에서 걷기·정지 애니메이션을 선택하고 정책 행동은 변경하지 않음.

기존 소화 테스트의 `StopRisk - StartRisk == 0.10` 근사 비교 허용오차를 `1e-6`으로 명시했다. float 뺄셈의 반올림 오차를 고려한 테스트 수정이며, 실제 소화 임계값·경계 비교는 변경하지 않았다.

## 검증 범위와 제한

- Mac UE 5.7 Editor Development 빌드 성공.
- 전체 자동화 67/67 성공: 일반 성공 63, 경고 포함 성공 4, 실패 0, 미실행 0. 프로세스 종료 코드 0.
- 지정 Blueprint 11개 컴파일 성공: 컴파일 오류 0, 경고 0, 로드 실패 0. 에셋은 저장하지 않았다.
- Prototype NullRHI 실행: NPC 20명, 화재 지연 3초, 경보 오프셋 1초, 약 63초 관측. 첫 실행은 8명 대피, 60fps 제한 재실행은 5명 대피였다. 두 실행 모두 행동불능 0명이다. 화재 시각 프레임은 검증 스크립트에서 8fps로 진행시켰으며 데이터 시간축 정합을 의미하지 않는다.
- 이전 NPC_merge 실행의 18명 대피보다 적다. 경보 인지·경로 요청·이동·양보·복구는 관측했지만, 감소 원인은 확정하지 않았다. 시작 산책의 무작위 목적지, 개인별 우선순위, 통로 정체 및 이동 제어를 같은 조건에서 추가 비교해야 한다. 원인을 확인하기 전 성능 회귀가 없다고 판정하거나 기존 원격 NPC_merge를 대체하지 않는다.
- 두 맵 실행에서 기존과 같은 WorldPartition EditorHash handled ensure와 종료 코드 1이 남았다. 최종 집계는 기록됐지만 오류 없는 에디터 실행은 아니다. 연기·열 로그도 0이므로 실제 화재 회피나 소화 성공을 검증한 결과가 아니다.
- 최초 새 테스트 실행의 난수 미초기화는 테스트 fixture에서 수정했다. 실제 BeginPlay 초기화 경로는 변경하지 않았다.
- 새 클론의 ONNX는 LFS 포인터였으므로 `git lfs install --local`과 모델 실파일 복원 후 실행한다.
- BIN/SVT 대응과 FDS 발화점 메타데이터는 여전히 별도 검증이 필요하다. 입력이 없거나 미확인이면 소화 동작이 발생하지 않을 수 있다.
- 이전 zion 문서의 Scenario_04 에셋 누락 설명은 당시 JJW 기준에 대한 기록이다. 이번 통합에서는 사용자가 확인한 NPC_merge의 기존 화재 에셋을 유지했다.
- 자동 테스트와 NullRHI 실행은 일반 에디터의 시각 확인 또는 Windows 빌드 검증을 대신하지 않는다.

## 재검증 명령

프로젝트 루트에서 실행한다. Windows에서는 실행 파일과 엔진 경로만 해당 환경에 맞게 바꾼다.

```sh
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" YUFSEditor Mac Development "$PWD/YUFS/YUFS.uproject" -WaitMutex -NoHotReload
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/YUFS/YUFS.uproject" -unattended -nop4 -NullRHI -DisablePlugins=MLAdapter -DisablePython '-ExecCmds=Automation RunTests YUFS.;Quit' '-TestExit=Automation Test Queue Empty'
```

MLAdapter 비활성화는 검증 프로세스의 서버 포트 충돌을 피하기 위한 명령행 옵션이며 프로젝트 설정 변경이 아니다. Mac의 상세 검증 산출물은 저장소 밖 `../NPC_Integration_2026-09-14`에 보관한다.
