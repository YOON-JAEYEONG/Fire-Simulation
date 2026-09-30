# JJW_NPC_BEHAVIOR 실행 안내

이 브랜치의 최신 실행 대상은 `YUFS/Content/Maps/Main.umap`이다. 이전 Prototype 장면과 다르다. Final_merge_2의 통합본을 기반으로 메뉴, NPC 배치, 실제 출구 판정, 화재 표시, NPC 위험 감지 연결을 수정한 버전이다.

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

`YUFS.uproject`를 우클릭하여 Visual Studio 프로젝트 파일을 생성한다. 생성된 솔루션에서 **Development Editor / Win64**, **YUFS**를 빌드한 뒤 `YUFS.uproject`를 연다. 엔진이 모듈 재빌드를 제안하면 빌드한다. 이 저장소는 C++ 소스 프로젝트이므로 각 컴퓨터의 엔진에 맞는 DLL을 최초 한 번 생성해야 한다. 이전 프로젝트의 Binaries를 복사하지 않는다.

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

UE 5.7.4 Windows 빌드, 기존 NPC 테스트 71개와 최신 위험/경로 테스트 5개, 실제 메뉴/팔레트 배치/화재 감지/계단 대피를 확인했다. 화재 BIN→화면 SVT 변환은 선택 프레임 비교에 근거한 경험적 보정이며 정밀 물리 단위/전체 시간 정합은 미확인이다. 일부 위험 경로 반복 거부와 벽/문 막힘은 남아 있다. 기본 60초 기록 종료 후 TimelineReview로 전환된다.

최신 설명: `YUFS/Docs/NPC_HAZARD_RESPONSE_20260930.md`. 과거 감사 문서의 “외부 BIN 별도 복사”, “OutsideDomain”, “아직 커밋하지 않음”은 당시 상태 기록이다. 현재 배포 방법은 이 문서를 따른다.
