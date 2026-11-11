# NPC 화재 감지 무반응 점검과 수정 (2026-09-30)

작업본: `C:/Users/ACSLAB4/Documents/Codex/Fire-Audit-0928/YUFS/YUFS.uproject`.
작업 브랜치: `codex/final-smoke-audit-windows`. 로컬 수정이며 커밋/푸시하지 않았다.

## 확인된 문제

Main의 화면 볼륨에는 SVT 프레임의 원점·반사·복셀 크기 변환이 적용된다. 기존 BIN 계산에는 이 변환 없이 볼륨 액터 변환과 `VoxelSize / abs(ScaleX)`만 적용됐다. 1·2번 BIN의 월드 높이는 -1001.667 ~ -310cm였고, 건물 NPC 눈 높이는 약 154/504cm여서 `OutsideDomain`, Self/Front/Above = 0이었다. 화면에 연기가 보여도 직접 위험 단서가 발생하지 않는 조건을 실제 Main Play에서 재현했다.

경보 이벤트도 실제 발생했다. 기존 방송 액터는 월드 원점, 반경 3000cm라 건물 전체를 포함하지 않았다. 사용자의 마지막 배치 위치별 경보 수신은 당시 기록하지 못했으므로, 이것이 그 실행의 전원 무반응 원인이었다고 단정하지 않는다. 별도 재현에서는 경보로 일부가 Preparing/Evacuating, 나머지가 Milling으로 전환됐다.

## 변경

- FireOption별 BIN→SVT 인덱스 변환 및 프레임 비율/오프셋을 추가했다. 데이터 전환마다 전부 갱신한다. 기존 맵은 기본값에서 이전 좌표 규칙을 유지한다.
- SVT 좌표 모드에서는 `BinaryGridToTexture × 해당 SVT 프레임 변환 × 볼륨 컴포넌트 변환`으로 감지 좌표를 계산한다. 필요 텍스처/변환이 없으면 InvalidMapping을 반환하고 기존 좌표로 몰래 돌아가지 않는다.
- Main의 세 FireOption에 실측 공간 보정을 저장했다. 1·2번은 SVT 인덱스 원점 `(7,-3,4)`, 3번은 `(2,3,1)`, 스케일 1, 프레임 비율 1/오프셋 0이다.
- 경보 액터를 건물 중앙 `(2150,-900,350)`으로 옮겼다. 반경 3000cm와 개인별 반응 모델은 유지한다.
- NPC 진단에 경보 수신/Actor Tick/현재 단계 활동 허용 여부를 추가했다. BIN 진단에는 좌표 모드와 데이터별 변환·시간 설정을 기록한다.

## 공간 보정의 근거와 한계

추가 변환 스크립트는 사용자에게 없다고 확인했다. 엔진에 포함된 OpenVDB 12로 세 데이터의 density/flame 복셀 값을 읽고, 프레임 100/250/480/750/996의 BIN과 비교했다. VDB 인덱스의 Z 기저는 -0.4이며, UE의 저장된 프레임 변환과 전역 시퀀스 bounds 최소값을 함께 사용해야 화면과 대응한다.

밀도 공간 상관의 주된 BIN→VDB 인덱스 이동은 1·2번 `(-5,-5,-1)`, 3번 `(-5,-4,-1)`이었다. 프레임 100~750의 상관은 약 0.856~0.944였다. Y축 등에서 한 복셀 정도 차이가 남고, BIN과 VDB 값은 일치하지 않는다. 이 값은 **경험적 공간 보정**이며 생성 스크립트로 입증된 정밀 변환이 아니다.

특히 1·2번 VDB의 마지막 프레임은 서로 같은 형태이며 BIN과 상관이 낮아진다. 마지막 프레임을 시간 오프셋의 증거로 삼지 않았다. 3번은 BIN 998/VDB 997프레임 차이도 남아 있다. 현재 60초 검사 범위는 대략 프레임 456~479까지이며 전체 시퀀스의 시간 정합 검증은 아니다.

`bDatasetAlignmentConfirmed=false`를 유지한다. 원본 FDS 변환 정보, 물리 단위, 발화 메타데이터가 확인됐다는 의미로 사용하면 안 된다. BIN 둘째 채널을 현재 열 단서로 읽는 기존 동작은 유지했지만 실제 섭씨 온도임을 새로 입증한 것은 아니다. 임의 사망 확률/노출 임계값/60초 종료 시간을 변경하지 않았다.

## 검증

- UE 5.7.4 Editor Development Win64 빌드 성공.
- 세 FireOption을 순서대로 전환한 추가 검사에서 모두 SVTFrame/Ready였고, 3번의 별도 원점과 BIN 경로가 갱신됐다. 이 추가 검사는 NullRHI로 설정/데이터 변환만 확인한 것이므로 보고서의 화면 bounds 값은 렌더링 정합 증거로 쓰지 않는다. 실제 렌더링/NPC 행동 검사는 아래 1번 화재 검사다.
- `YUFS.NPC.Navigation.Hazard` 테스트 5개 통과 (좌표·층, 위험 경로/위험 장소 탈출, Detour, 개인 기억, 스트리밍).
- 저장 후 다시 읽은 Main의 일반 PIE에서 같은 팔레트 클래스의 NPC 6명을 배치해 58초 기록했다. 기존 네 위치와 화재 근처 두 위치를 사용했다. 보정 후 두 층 눈 위치가 Ready가 되었으며 직접 Smoke/Heat 단서로 Evacuating 전환을 확인했다. 6명 중 4명은 유효 출구에 도달했다. 같은 네 NPC만의 완전한 동일 조건 통계 실험이나 전원 대피 보장은 아니다.
- 화재 근처 NPC는 초기 Smoke 약 0.50 / Heat 약 0.10 단서 후, Smoke 약 0.94 / Heat 약 0.48에서 Heat 단서로 대피했다. 실제 노출도 누적되었다.
- 일부 NPC는 UnsafePath로 경로를 거부하고 반복 재시도했으며 한 NPC는 이동이 막혔다. **직접 위험 감지 복구와 막힘 해소는 별개의 결과**다. 위험한 경로를 강제로 통과시키는 변경은 하지 않았다.

실제 Windows UI에서도 검증했다. Main 일반 Play → 게임 팔레트의 Default를 방 바닥으로 드래그 → 방향 확인 → HUD 시작을 누른 흐름이다. 배치된 NPC는 Tick=1로 복원되었다. 경보 신뢰도 0.08인 NPC가 경보 후 Perceiving/Milling으로 기다리다가, 화재 시작 약 10초 후 Smoke 단서로 Preparing, 약 13초 후 Evacuating으로 전환하고 약 19초 후 계단을 내려가 유효 출구에 도착했다. 60초 UI 결과는 대피 1/전체 1이었다. 검사 위치의 결과이며 모든 방/모든 NPC의 성공을 보장하지 않는다.

증거: `Audit0928Evidence/fire_field_registration.json`, `npc-response-before.json`, `npc-response-after.json`, `npc-response-after.log`, `hazard-alignment-tests.log`, `save_fire_calibration.json`, `live_npc_response_observer.json`, `npc-response-ui.log`.
