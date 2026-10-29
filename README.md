# JJW_NPC_BEHAVIOR 실행 안내

이 브랜치의 최신 실행 대상은 `YUFS/Content/Maps/Main.umap`이다. `merge_JJW_Kim_Eung_Gang@83cb710` 통합본을 기반으로 NPC 경로 탐색 완성 단계 1~11과 배포 점검을 반영했다. 메뉴의 **Main (화재 시뮬레이션)**에서 확인한다.

## Windows에서 처음 받기

Unreal Engine **5.7.4**, C++ 빌드 도구(Visual Studio의 C++ 게임 개발/Windows SDK), **Git LFS**가 필요하다. 검증 플랫폼은 Windows이며 Mac 빌드는 이번에 검증하지 않았다.

```powershell
git lfs install
git clone --branch JJW_NPC_BEHAVIOR --single-branch https://github.com/YOON-JAEYEONG/Fire-Simulation.git
cd Fire-Simulation
git lfs pull
git branch --show-current
git rev-parse HEAD
```

브랜치 출력이 `JJW_NPC_BEHAVIOR`인지 확인한다. GitHub ZIP 다운로드 대신 Git LFS를 사용하는 클론을 권장한다. LFS 다운로드 오류가 있으면 실행하지 말고 먼저 해결한다.

프로젝트 에디터를 닫고 저장소 최상위의 **`Build-And-Launch-YUFS-Safe.cmd`**를 실행한다. 해당 폴더의 데이터·맵을 검사하고 **Development Editor / Win64**를 빌드한 후 바로 그 폴더의 `YUFS.uproject`를 연다. 첫 클론 및 C++ 변경을 받은 뒤 이 파일을 사용한다. 빌드 도구가 설치되어 있어야 한다. 엔진 설치 위치가 다르면 환경 변수 `YUFS_ENGINE_ROOT`를 UE_5.7 폴더로 지정한다.

직접 빌드하려면 `YUFS.uproject` 우클릭으로 Visual Studio 프로젝트 파일을 생성하고 **Development Editor / Win64**, **YUFS**를 빌드한다. 각 컴퓨터의 엔진에 맞는 DLL이 필요하므로 다른 프로젝트의 Binaries를 복사하지 않는다. 빌드한 뒤 소스 변경이 없을 때는 `Launch-YUFS-Safe.cmd`로 다시 열 수 있다. 두 실행 파일은 공용 `C:\CodexWork` 연결을 사용하지 않는다.

다운로드만 검사하려면 다음을 실행한다. 실제 데이터 SHA256, Unreal 맵·SVT 파일, 현재 브랜치·커밋을 확인한다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Test-YUFSCheckout.ps1 -ExpectedBranch JJW_NPC_BEHAVIOR
```

## 필요한 화재 파일

저장된 Main에 연결한 SVT와 머티리얼 6개는 `YUFS/Content/Fires/Audit0928/`에 포함되어 있다. 원본 VDB ZIP을 다시 다운로드하거나 임포트할 필요는 없다. 감지용 BIN 3개도 이번부터 Git LFS로 포함한다:

| 상대 경로 (YUFS/Content 기준) | 바이트 수 |
| --- | ---: |
| Fires/FirePrototype/BinaryData/smoke_data_it_test_1.bin | 245182256 |
| Fires/FirePrototype/BinaryData/smoke_data_it_test_2.bin | 245182256 |
| Fires/FirePrototype/BinaryData/smoke_data_it_test_3_v1.bin | 245428176 |

합계 약 736 MB이며 Git LFS 다운로드가 완료되어야 한다. 개별 SHA-256은 `YUFS/Docs/CLONE_DATA_SHA256.json`을 참고한다. 몇백 바이트짜리 파일이면 실제 데이터가 아니라 LFS 포인터일 수 있다.

## 시뮬레이션

1. 기본 메뉴 `Lvl_MainMenu`에서 Play → 새 시뮬레이션 → **Main (화재 시뮬레이션)**을 선택한다.
2. 게임 안의 NPC 목록에서 NPC를 건물의 바닥으로 드래그하고 방향을 확정한다. Main은 자동으로 20명을 생성하지 않는다. 콘텐츠 드로어에서 별도의 Spawner를 추가하는 방식과 다르다.
3. HUD 시작을 누른다. 메뉴 기본 설정은 화재 전 30초 대기이다.
4. PLAYER 모드에서 건물 화면을 클릭하고 W/마우스로 조작한다.
5. NPC는 경보, 연기, 열 단서와 개인 성향에 따라 반응한다. 전원이 동시에 즉시 대피하는 것은 아니다. Play 중 배치는 종료하면 사라진다.

화재 옵션, NPC 위치/성향, 품질 설정, 기기 성능이 다르면 결과도 달라질 수 있다. 동일 소스/맵/데이터를 배포하며 모든 실행의 이동 결과가 완전히 같다는 보장은 아니다.

## 검증과 남은 문제

UE 5.7.4 Windows 빌드와 전체 자동 검사 **119개**(NPC115개 포함)를 확인했다. 개인별 경보·연기·열 인지, 연기에 따른 시야 감쇠, 개인 출구 기억/탐색/안내, 위험 경로 재검사, 양보·대기·물리 복구, 실제 낮은 자세, 출구 영역 판정, 보이는 행동 불능 상태, 일시정지·타임라인 재생·과거 HUD·재시작을 포함한다. 이동 결과를 맞추기 위한 강제 대피/사망 처리는 넣지 않았다.

화재 BIN→SVT 변환은 경험적 보정이며 물리 단위·출처·전체 시퀀스 정합은 아직 미확인이다. `bDatasetAlignmentConfirmed=false`를 유지한다. 확인된 발화 위치 메타데이터가 없는 화재에는 자율 소화를 허용하지 않는다. 명시적인 소화 동작 프리뷰는 실제 화재 인식·진압 성공과 별개이며 원본 FDS 재생을 바꾸지 않는다.

기본 화재 후 60초에 TimelineReview로 바뀐다. 그때 미해결 NPC는 성공/사망으로 자동 분류하지 않는다. 리뷰 중 상세 감지/경로 조회는 멈춘 현재 상태일 수 있으며, 과거 인원·시간은 HUD와 기록 프레임으로 확인한다. 모든 고밀도 교착이나 맵의 모든 문을 해결했다는 보장은 아니다.

`Launch-InteractionPreview.ps1`, `Launch-NaturalNpcInteractions.ps1` 등의 Prototype 시연 실행 파일은 일반 Main 시뮬레이션의 실행 경로가 아니다.

최신 검증 조건·결과는 `YUFS/Docs/NPC_NAVIGATION_RELEASE_20261018.md`, 단계별 구현은 `YUFS/Docs/NPC_NAVIGATION_STEP*_*.md`를 참고한다. 과거 문서의 “외부 BIN 별도 복사”, “아직 커밋하지 않음” 등은 당시 상태 기록이다. 현재 배포 방법은 이 README를 따른다.
