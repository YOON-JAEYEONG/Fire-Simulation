# 기존 화재를 대상으로 하는 NPC 상호작용

> 최신 구현·확률·검증 상태는 [지정 방 화재 효과 및 확률 소화](LOCAL_FIRE_EFFECT_AND_RATIO_KO.md)를 따른다.
> 아래 미검증/미설정 설명은 연결 작업 당시의 기록이다.

## 사용자 지정 발화점 (2026-09-08)

표시된 왼쪽 위 방의 2층 바닥을 화면 역투영 및 충돌 검사로 확인했다.
월드 좌표 `(-309.980809, -257.265525, 351.0)`을
`Config/DefaultGame.ini`의 `[YUFS.PrototypeFireTarget]`에 저장했다.
Prototype 재생 시 기존 화재 볼륨에 적용하며, 새 불을 생성하지 않는다.
아래 미설정 안내는 좌표 지정 이전 상태다. VDB/연기 데이터 누락은 별도 문제로 남아 있다.

## 현재 상태

기존 대피 코드는 유지했다. 소화 목표를 임시 `AYUFSSuppressibleFireSource`에서
맵의 기존 `AYUFSHeterogeneousVolume`으로 변경했다. 새 화재 액터는 생성하지 않는다.
코드는 빌드 및 자동 테스트 대상이며, 원본 화재 데이터가 없어 실제 발화점에서의
획득→이동→분사 장면은 아직 검증하지 못했다.

## 원본 확인

참조: `origin/IT_YOON_JAEYEONG`, 커밋 `4293ca3ec148bfc2a5edaf3d949dbcef6e2666b0`.

- `YUFS/Source/YUFS/Fire/YUFSHeterogeneousVolume.cpp`: 사전 계산 화재 프레임의 재생/정지 제어.
- `YUFS/Content/__ExternalActors__/Maps/Prototype/4/95/9ZN4TGUM1KY0UT2A3C5GZ0.uasset`:
  기존 화재 볼륨과 `Scenario_1_MIC` 머티리얼 참조.
- `YUFS/Content/Fires/FirePrototype/VDB/Scenario_1_MIC.uasset`:
  `/Game/Fires/vdb/Scenario_01/vdb/fire` 텍스처 참조. 해당 텍스처 파일은 로컬에 없다.
- `YUFS/Content/__ExternalActors__/Maps/Prototype/D/1S/4TDPMBXAI64KGJ92MDIZLI.uasset`:
  `Fires/Scenario_01/smoke_data.bin` 참조. 해당 파일은 로컬에 없다.
- 원본 `.gitignore`는 `.bin` 및 위 화재 텍스처를 제외한다. Git fetch로는 복구되지 않는다.

## 연결 방법

기존 화재 볼륨의 `Fire > Interaction`에서 `bHasInteractionTarget`을 켜고
`InteractionTargetLocal`을 실제 발화점의 볼륨 로컬 좌표로 설정한다.
볼륨은 100배 스케일과 반전 축을 사용하므로 월드 좌표를 로컬 값에 그대로 넣으면 안 된다.
목표는 `GetActorTransform().TransformPosition(InteractionTargetLocal)`로 변환한다.
미설정일 때는 좌표를 추정하거나 임시 불로 대체하지 않는다.

## 두 행동을 함께 보여주는 모드

`Launch-NaturalNpcInteractions.ps1`의 `-YUFSBuildingInteractions`에서
기존 주민 ID 0과 3만 훈련자 시연 후보로 설정한다. 별도 NPC를 생성하지 않는다.
이 둘은 시연을 위한 발화 위치 사전 지식을 가지되 소화기는 직접 시야로 발견해야 한다.
유효한 목표·소화기·퇴로가 있으면 선택 확률을 1로 설정한다.
이는 발표용 조건이며 실증 행동 비율이 아니다. 일반 실행에서는 이 설정을 사용하지 않는다.

다른 주민은 기존 정책으로 대피한다. 선택된 주민은 소화기 예약→획득→도달 가능한
이격 지점 이동→몸 회전/준비→6초 분사→대피 의도 복귀 순으로 행동한다.
타임아웃(90초), 약제 소진, 퇴로 상실, 공식 안내, 즉각적인 생명 위험은 중단 조건이다.

분사 완료 결과는 `SuppressionActionCompleted`이다. 사전 계산 VDB에 진압량을 반영하지
않으므로 `FireExtinguished`라고 기록하거나 불이 실제로 꺼졌다고 판정하지 않는다.
전용 소화 애니메이션/양손 IK는 아직 없으며 기존 임시 모션을 사용한다.

## 검증

UE 5.7 `YUFSEditor Win64 Development` 빌드 성공.
`Logs/ExistingFireContractTests.log`: NPC 자동 테스트 10개.
새 검사는 목표 미설정 시 대피, 유효한 기존 목표 선택, 퇴로 상실 시 중단,
상호작용 종료 후 대피/대기 의도 복귀를 확인한다.
이는 결정 로직 검증이지 실제 건물에서의 전체 경로·모션 검증을 대신하지 않는다.
