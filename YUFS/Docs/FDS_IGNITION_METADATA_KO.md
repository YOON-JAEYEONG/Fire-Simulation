# FDS 발화점 메타데이터 계약

## 목적과 범위

NPC의 소화 **시도 위치**를 FDS 원본에서 검토한 좌표에 연결한다. 이 로더는 FDS/BIN/SVT를 수정하거나 화재를 진압하지 않는다. 연소 강도, 연기, 온도, 독성, 기존 표시 효과에 쓰는 데이터에도 값을 쓰지 않는다.

기준 커밋: `225a1e7ebf498ab0190fad647b5eab15bf8789dc`. 이 계약은 BIN/SVT의 공간·시간·축 정합을 알 수 없으면 추측하지 않는 원칙을 따른다. 현재 확보한 BIN만으로 발화점이나 축 반전을 복원했다고 주장하지 않는다.

현재 실행 통합 기준은 `JJW_NPC_BEHAVIOR`의 `204a8d3`이다. [JJW 우선 병합 안내](JJW_PRIORITY_MERGE_KO.md)를 함께 확인한다. JJW의 일반 대피·BIN 표본 API는 원래 동작을 유지하며, 아래 정합 확인은 **선택적 소화 시도를 허용하는 추가 조건**이다. 일반 대피의 원본 지각·경로 전체를 이 플래그로 차단하지 않는다.

이 변경에는 **활성 예제 좌표나 가짜 FDS 메타데이터 파일을 넣지 않는다**. 원본 FDS와 검증된 정합 정보가 없으면 소화 대상 좌표를 제공하지 않는다. 대피 로직은 별도로 계속 사용할 수 있다.

현재 이 작업에서 원본 `.fds`, `.smv` 또는 검토 완료된 좌표·시간 매핑은 제공받지 못했다. `smoke_data.bin`의 존재만으로 이 정보가 확인되지는 않는다. 따라서 이 변경만으로 실제 FDS 위치에 소화 시도가 연결됐다고 보거나 두 정합 확인 값을 켜면 안 된다.

## 서로 독립적인 두 데이터 조건

| 조건 | 검증 대상 | 통과해도 보장하지 않는 것 |
|---|---|---|
| JSON `alignmentConfirmed=true` 및 메타데이터 파싱 성공 | 발화원 ID·좌표와 명시적인 UE 좌표/시뮬레이션 시간 변환 계약 | BIN의 복셀 축·단위·프레임 시간·위험값 정합, 실제 원본 해시 일치 |
| BinaryManager `bDatasetAlignmentConfirmed=true` 및 유효 프레임 적재 | 담당자가 별도로 확인한 기존 BIN 샘플링 정합과 현재 프레임의 읽기 가능 여부 | 발화점 좌표의 존재, NPC의 발화점 발견, 원본 FDS 검증, 자동적인 소화 행동 |

- 하나를 켰다고 다른 조건을 자동으로 켜지 않는다. 소화 실행기는 BIN 정합 미확인·파일 오류·미적재 프레임이면 선택적 소화 접근을 승인하지 않는다. JJW의 일반 표본 API에서 Ready를 받았다는 것만으로 정합까지 검증됐다고 해석하지 않는다.
- 발화점 계약이 유효하더라도 활성 시각에 도달하지 않았다면 목표는 아직 제공되지 않는다. 이것은 누락된 좌표 설정 오류와 구분한다.
- 실제 소화 시도에는 위 데이터 조건 외에도 NPC 감지·판단·도구·경로·위험 제한을 통과해야 한다. 전역적으로 좌표를 알고 행동하도록 강제하지 않는다.
- 기존 녹화 볼륨의 표시 재생은 소화 효과로 감소하지 않는다. 상호작용 설정이 기존 화재 시작 카운트다운을 임의의 1초로 바꾸지도 않는다. 검토한 시간 매핑은 실제 설정된 시뮬레이션 시계에 맞춰야 한다.

## 데이터와 인지의 분리

```
검토한 FDS 원본 → 명시적인 JSON 좌표·시간 계약 → 월드 위치
                                               ↓
                                 기존 NPC의 감지·시야·위험 판단
                                               ↓
                           소화 시도 또는 위험에 따른 대피
```

- 좌표 제공은 NPC가 발화점을 알고 있다는 뜻이 아니다. NPC의 실제 발견, 시야·층·경로 검사, 도구 보유·예약, 위험 판단은 기존 계층에서 처리한다.
- 메타데이터의 활성 시간은 대상 좌표를 사용할 수 있는 가장 이른 시점이다. 실제 연소 여부, HRR 변화, 자연 소멸이나 소화 성공을 입증하지 않는다.
- 이 계약은 한 번에 선택한 발화원 하나만 반환한다. 여러 발화원 가운데 가까운 것/가장 큰 것/첫 번째 것을 임의로 택하지 않는다.
- 원본 FDS 문법 전체 파서, `VENT`/`OBST`의 열방출 조건 추론, BIN에서 발화원 역추정은 구현하지 않는다.

## schemaVersion 1

| 필드 | 형식 | 검증 및 의미 |
|---|---|---|
| `schemaVersion` | 숫자 `1` | 다른 버전은 거부 |
| `sourceFile` | 비어 있지 않은 문자열 | 검토한 FDS 원본의 파일명 또는 추적 가능한 경로 |
| `sourceSha256` | 64자리 16진수 | 해당 원본 바이트의 SHA-256 식별값 |
| `alignmentConfirmed` | 불리언 `true` | 담당자가 좌표·단위·시간 정합을 확인한 후에만 설정 |
| `units` | 문자열 `m` | FDS 위치 입력은 미터 |
| `coordinateMapping` | 객체 | 아래의 좌표 변환을 빠짐없이 명시 |
| `timeMapping` | 객체 | 아래의 시간 변환을 빠짐없이 명시 |
| `selectedIgnitionId` | 문자열 | 발화원이 여러 개면 필수, 한 개면 생략 가능 |
| `ignitionSources` | 1개 이상의 객체 배열 | ID 중복 금지, 모든 레코드를 검증 |

`sourceSha256`은 파서에서 **형식만 확인**한다. 이 JSON만 읽고 원본 파일의 내용이나 사실성이 검증됐다고 간주하지 않는다. 배포 전 담당자가 실제 원본 파일의 해시를 산출해 대조해야 한다. 런타임은 원본 FDS를 임의 경로에서 읽거나 수정하지 않는다. 이 메타데이터는 신뢰된 프로젝트 설정이지 인증된 외부 입력 형식이 아니다.

### 좌표 변환

`coordinateMapping`의 모든 벡터는 유한한 숫자 3개의 배열이다.

| 필드 | 의미 |
|---|---|
| `originUeCm` | FDS 원점이 위치하는 **UE 월드 좌표**, 단위 cm |
| `axisX` | FDS +X가 향하는 UE 월드 방향, 길이 1 |
| `axisY` | FDS +Y가 향하는 UE 월드 방향, 길이 1 |
| `axisZ` | FDS +Z가 향하는 UE 월드 방향, 길이 1 |
| `scaleCmPerMeter` | 각 FDS 축에서 1m가 UE에서 몇 cm인지 나타내는 양수 3개 |

변환식은 아래 **한 번만** 적용한다.

```text
P_ue_cm = originUeCm
        + axisX × scaleCmPerMeter.x × P_fds_m.x
        + axisY × scaleCmPerMeter.y × P_fds_m.y
        + axisZ × scaleCmPerMeter.z × P_fds_m.z
```

- 보통 미터→센티미터의 크기는 100이지만 기본값으로 추정하지 않는다. `[100,100,100]`도 정합을 확인해 명시해야 한다.
- 3축은 서로 직교하는 단위 벡터여야 한다. 길이 제곱과 축 간 내적의 허용 오차는 `1e-4`이다. 비정규화 축과 전단(shear)은 거부한다.
- 반사는 방향 벡터에 명시하며 허용한다. 예를 들어 특정 축을 반전할지 여부를 BIN/SVT나 메시에 보고 자동 결정하지 않는다.
- 양의 비균등 스케일은 명시된 경우 허용한다. 음수 스케일은 거부한다. 반전은 축 벡터로 표현해 의미를 분명히 한다.
- 출력은 이미 월드 좌표다. 호출자가 Volume의 `ActorTransform`을 다시 적용하면 안 된다.
- 건물 메시를 옮기거나 크기를 바꾸면 정합을 다시 검토해야 한다. 로더는 메시 배치가 원본과 일치하는지 검사하지 않는다.

### 시간 변환

`timeMapping`의 필수 필드:

| 필드 | 의미 |
|---|---|
| `referenceClock` | 정확히 `simulationElapsedSeconds` |
| `fdsTimeAtSimulationZeroSeconds` | 시뮬레이션 경과 0초에 대응하는 FDS 시각 |
| `fdsSecondsPerSimulationSecond` | 시뮬레이션 1초당 FDS 시간 증가량, 유한한 양수 |

```text
t_fds = fdsTimeAtSimulationZeroSeconds
      + SimulationController.GetElapsedTime() × fdsSecondsPerSimulationSecond

대상 사용 가능: t_fds >= ignitionSource.activationTimeSeconds
```

- FDS 시간 원점을 플레이 시작, 화재 시작 카운트다운 완료, SVT 첫 프레임과 자동으로 같다고 가정하지 않는다.
- SVT `Frame / PlaybackFrameRate`를 FDS 시간으로 바꾸지 않는다. JSON은 이 참조 시계를 허용하지 않는다.
- `GetElapsedTime()`의 일시정지/리셋 동작을 사용한다. 호출부는 화재 단계·시뮬레이션 실행 가능 여부도 별도로 검사해야 한다.
- 음수·NaN·무한 시뮬레이션 시각과 시간 변환 오버플로는 거부한다.

### 발화원 레코드

| 필드 | 의미 |
|---|---|
| `id` | 원본과 대조할 수 있는 비어 있지 않은 고유 ID |
| `fdsPositionMeters` | 검토한 FDS 발화원 기준점 `[x,y,z]`, 단위 m |
| `activationTimeSeconds` | 이 발화원에 대한 FDS 기준 활성 시각, 0 이상 |

원본이 점이 아니라 면/영역인 경우 어떤 점을 소화 목표로 사용할지 FDS 담당자가 정하고 기록한다. 로더가 영역 중심이나 바닥 높이를 임의로 추정하지 않는다. NPC가 설 위치는 이 점 자체가 아니라 별도의 경로·시야 검사에서 정한다.

여러 발화원 중 하나를 선택했더라도 미선택 레코드의 잘못된 축적 데이터나 중복 ID를 묵인하지 않도록 전체 레코드의 기본 형식을 검사한다. `selectedIgnitionId`가 실제 ID와 일치하지 않으면 실패한다.

## C++ 연결

```cpp
FYUFSFdsIgnitionMetadata Metadata;
FString Reason;
if (!FYUFSFdsIgnitionMetadata::LoadFile(ReviewedMetadataPath, Metadata, Reason))
{
    // 이유를 기록하고 소화 위치 제공을 중단. 하드코딩 좌표로 대체하지 않음.
}

FVector WorldTarget;
if (Metadata.TryGetActiveIgnition(SimulationController->GetElapsedTime(), WorldTarget, Reason))
{
    // 기존 감지/시야 검사로 전달. 모든 NPC의 Belief에 무조건 주입하지 않음.
}
```

`ParseJson()`은 파일 시스템이나 월드 객체에 접근하지 않는 순수 파서이다. `LoadFile()`은 최대 1MiB의 sidecar를 읽고 **같은 파서**에 전달한다. 파싱 또는 로딩에 실패하면 이전의 유효한 메타데이터도 지운다. 위치 조회 실패 시 `OutWorldLocation`을 0으로 초기화하지만, 이는 원점이 목표라는 의미가 아니므로 반드시 불리언 반환값을 검사한다.

`GetSelectedIgnitionId()`, `GetSourceFile()`, `GetSourceSha256()`으로 로그에 선택 대상과 출처를 남길 수 있다. 로그의 해시 표시는 원본 진실성 검증 완료를 의미하지 않는다.

주요 실패 이유: `AlignmentUnconfirmed`, `InvalidCoordinateMapping`, `NonOrthonormalAxes`, `InvalidScale`, `InvalidTimeMapping`, `AmbiguousIgnitionSource`, `SelectedIgnitionNotFound`, `IgnitionNotActiveYet`. 전체 코드는 `FdsMetadata.` 접두어를 사용한다.

## 실제 데이터 연결 전 체크리스트

1. 실행할 FDS 원본 및 해당 BIN/SVT의 생성 관계를 확보한다.
2. 실제 원본의 SHA-256을 구해 `sourceFile`과 함께 기록한다.
3. 발화원 ID·기준점·활성 시각을 원본과 대조한다.
4. 최소한 원점과 3축 방향·길이를 건물의 알려진 기준점으로 교차 확인한다. 층 높이도 확인한다.
5. FDS 시간과 시뮬레이션 경과 시간의 관계를 확인하고 timeMapping을 적는다.
6. 검토 완료 후에만 `alignmentConfirmed=true`인 실제 sidecar를 연결한다.
7. 담당자와 함께 예상 방/층에 목표가 있는지 확인하고, NPC 감지 이전에는 행동으로 이어지지 않는지 검사한다.
8. 위험 상승·도구 고갈·경로 상실 시 소화 시도가 중단되고 기존 대피 판단으로 복귀하는지 검사한다. 소화 성공으로 연기 데이터나 FDS 값이 감소해서는 안 된다.

## 자동 검사 범위

테스트 그룹 `YUFS.Fire.FdsIgnitionMetadata`는 자산·월드 없이 다음을 확인한다.

- 명시적 반사/비균등 스케일과 시간 변환 경계
- 누락/미확인 정합, 단위, 원본 해시 형식, 축/스케일/시간 매핑 오류
- NaN/무한/변환 오버플로, 잘못된 입력 형식
- 다중 발화원 모호성, ID 중복, 존재하지 않는 선택
- 로딩/파싱 실패 후 이전 좌표로 돌아가지 않는 fail-closed 동작

테스트의 작은 합성 좌표는 파서 검증 전용이다. 실제 건물의 발화 위치나 FDS 정합을 검증한 것이 아니며 런타임 설정으로 설치하지 않는다.

## 2026-09-10 GitHub 원본 조사 결과

사용자가 지정한 저장소의 원격 14개 브랜치 최신 트리와 도달 가능한 전체 이력 51개 커밋을 확인했다. 기본 브랜치는 `main` (`a408baa615461872f50f289b4789fdd11cd7df12`), 조사한 최신 IT 브랜치는 `7e669ffc7dff8994907fb83855d817620dd257ef`이다. `.fds`, `.smv`, 발화점 메타데이터, 관련 압축파일에서 원본을 찾지 못했다. 발견한 ZIP은 소화기 모델용이었다. 이는 GitHub에 게시된 확인 범위의 결과이며 팀원 PC에 원본이 없다는 뜻은 아니다.

| 확인한 저장소 파일 | 확인한 사실 / 연결에 부족한 정보 |
|---|---|
| [FDS2VTK.py](https://github.com/YOON-JAEYEONG/Fire-Simulation/blob/7e669ffc7dff8994907fb83855d817620dd257ef/YUFS/Content/Fires/FirePrototype/BinaryData/FDS2VTK.py#L92) | 기본 입력은 외부 `C:\FDS_6FLOOR`이다. SMV에서 축 좌표를 읽지만 `exportVTK`는 값만 쓴다. 실제 시간 대신 순번 파일명을 남긴다. 이 입력 폴더는 현재 PC에 없다. |
| [VTK2CSV_fixed.py](https://github.com/YOON-JAEYEONG/Fire-Simulation/blob/7e669ffc7dff8994907fb83855d817620dd257ef/YUFS/Content/Fires/FirePrototype/BinaryData/VTK2CSV_fixed.py#L14) | 격자 `181×79×18`, 간격 `0.4m`, 원점 `0`을 코드로 지정한다. `temperature` 열에 `HRRPUV`를 넣으므로 이름만 보고 섭씨 온도라고 해석할 수 없다. 이는 실제 건물 UE 월드 정합의 증거가 아니다. |
| [convert_to_binary.py](https://github.com/YOON-JAEYEONG/Fire-Simulation/blob/7e669ffc7dff8994907fb83855d817620dd257ef/YUFS/Content/Fires/FirePrototype/BinaryData/convert_to_binary.py#L16) | 프레임 수·격자 차원의 16바이트 헤더와 정렬된 두 uint8 필드만 기록한다. 원점·발화원·실제 타임스탬프·UE 변환을 저장하지 않는다. |
| [.gitignore](https://github.com/YOON-JAEYEONG/Fire-Simulation/blob/7e669ffc7dff8994907fb83855d817620dd257ef/.gitignore#L79), [.gitattributes](https://github.com/YOON-JAEYEONG/Fire-Simulation/blob/7e669ffc7dff8994907fb83855d817620dd257ef/.gitattributes) | BIN/CSV와 Fires 디렉터리에 제외 규칙이 있다. 확인한 LFS 대상은 모델·학습 전이로그이며 FDS 원본의 별도 배포 근거를 찾지 못했다. 이미 추적된 일부 자산은 제외 규칙과 별개로 남아 있다. |
| [기준 커밋 CODEX_HANDOFF.md](https://github.com/YOON-JAEYEONG/Fire-Simulation/blob/225a1e7ebf498ab0190fad647b5eab15bf8789dc/CODEX_HANDOFF.md#L32) | BIN `8000 / 153×115×17`과 SVT `514 / 361×158×36`의 위치·시간·축·정규화 대응이 미확정임을 명시하고, 추측으로 비율을 바꾸지 말라고 한다. |

[게시된 GitHub 릴리스](https://github.com/YOON-JAEYEONG/Fire-Simulation/releases)에서도 추가 데이터 배포를 찾지 못했다. 따라서 저장소만으로 최대값 복셀을 발화점이라고 정하거나 이전 수동 좌표를 FDS 좌표로 이름만 바꾸지 않았다. `MetadataFile`은 비워 두고 원본/정합 공급을 기다린다. 필요한 다음 자료는 **실제 생성에 사용한 `.fds`·`.smv` 또는 검토된 발화원 좌표/활성시각 및 그 데이터의 UE 공간·시간 변환 기록**이다.
