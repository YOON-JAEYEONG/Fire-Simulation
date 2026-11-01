# merge_final 건물 문·비상구 적용

기준: origin/merge_final `f03f290`. 구현은 `codex/merge-final-doors-exits`에서 진행했고, 사용자 요청에 따라 배포 브랜치 `door_merge`를 생성했다.

- [x] 기존 zion 작업 보존 및 최신 merge_final 기준 작업 브랜치 생성.
- [x] Main과 최종 건물 Main_level의 형상·기존 문·출구 조사.
- [x] 실제 개구부에 상호작용 문과 식별 가능한 비상구 배치.
- [x] 저장·내비게이션 재빌드·실행 검증.
- [x] 해당 맵을 언리얼에서 열어 직접 관찰할 수 있게 준비.

상태: 기존 merge_final의 NPC·화재·UI 계약을 유지하며 맵 배치를 적용했다. 기존 zion 맵은 보존한다. 사용자의 `door_merge` 브랜치 생성 및 GitHub 업로드 요청에 따라 맵·에셋·재현 스크립트·검증 기록을 커밋/푸시한다.

## 적용 내용

대상 `/Game/Maps/it관_찐최종/Main_level`. 이 건물은 기존 Main과 좌표·규모가 다르다. 원본의 수평 벽 단면 Z=120/470cm를 추출하여 기존 출입 틈을 조사했고, 계단 홀·중앙 로비를 제외한 1층14개·2층16개 방문에 기존 `AYUFSInteractionDoor`를 배치했다. 근거 좌표는 `Content/Python/MergeFinalDoorPlan.json`에 보관한다. 문 폭은 해당 벽 틈에서 문틀 크기를 고려해 산정한 시뮬레이션 배치값이며 현장 문 사양을 확정한 값은 아니다.

1층 바닥 기준 Z=20cm. 서쪽(2403.824,-8673.75), 동쪽(9593.824,-8673.75), 중앙(5983.824,-7283.75) 출입구에 초록색 양면 EXIT 표지·문틀·실외 지지 바닥과 `AYUFSExitPoint`를 연결했다. 결과 판정은 merge_final의 기존 캡슐·지지면 검사 방식을 사용하며 이번 작업에서 NPC 의사결정/성공 기준을 변경하지 않았다.

동쪽은 원래 막힌 벽이므로 폭200×높이225cm 개구부를 생성했다. 겹친 네 벽 레이어의 원본 임포트 에셋은 보존하고 `/Game/YUFS/MergeFinal/SM_EastExitWall_0..3` 복사본만 수정하여 해당 액터에 연결했다. 크기는 시뮬레이션 작업 가정값이다. 원본 Main_level 파일의 추가 복사본은 `Saved/Backups/MergeFinalDoors20261101/Main_level_before.umap`에 있다.

## 검증

- merge_final 기준 C++ 편집기 빌드 성공. C++ 소스 변경 없음.
- 최종 맵 저장 및 내비게이션 재빌드 완료.
- 화면 없는 PIE: 문30개/출구3개 존재, 대표 방문은 닫힌 상태에서 캡슐 이동 차단 → 실제 문 개방 → 통로 가드 해제 → 캡슐 이동 통과.
- 세 출구 각각 안쪽200cm에서 바깥쪽200cm까지 캡슐 통과 및 완전한 내비게이션 경로 확인(partial=false). 모든 방/다인 상황을 장시간 반복 검증한 결과는 아니다.
- 검증 스크립트 `verify_merge_final_doors_exits.py`, 로컬 결과 `Saved/MergeFinalVerification.json`, 로그 `Logs/MergeFinalVerify20261101c.log`.
- 문 개방 검사에서는 임시 PIE에서만 화재 시작 대기를0초로 두어 동작을 검사했다. 맵에 저장한 방송·화재·NPC 규칙은 변경하지 않았다. 기존 기본 smoke_data.bin 누락/발화점 메타데이터 누락 경고는 별도 남아 있으며 새 건물의 FDS 정렬 검증은 이번 범위에 포함하지 않는다.

## 재현/관찰

배치: `setup_merge_final_doors_exits.py` → `open_merge_final_east_exit.py`(GeometryScripting 플러그인을 해당 실행에 활성화) → `bake_merge_final_navigation.py`. 각 스크립트는 본인이 추가한 배치만 다시 적용한다.

관찰: 편집기를 `-ExecCmds="py C:/CodexWork/FireSimulationWorkspace/YUFS/Content/Python/preview_merge_final_doors_exits.py"` 옵션으로 열거나, 편집기 콘솔에서 같은 `py` 명령을 실행하면 최종 맵의 PIE에 들어간다. `-ExecutePythonScript`는 스크립트 종료 후 편집기까지 닫는 동작이 있어 관찰용 실행에는 사용하지 않는다. 기존 NPC가 없으면 양층에 최대20명을 임시 배치하며 NPC를 맵에 저장하지 않는다. 시뮬레이션 시작은 사용자가 시작 버튼을 누른다. 자유시점은 기존 조작을 사용한다.

최종 관찰 준비 확인: `Logs/MergeFinalUserPreview20261101b.log`에 occupants=20 기록. 실제 렌더링 화면에서 최종 건물·방문·중앙 EXIT 표시와 대기 중/전체20 UI 확인. 편집기를 열어 둔 상태이며 시작 버튼은 사용자가 누르도록 남겨 두었다. 맵의 양층 바닥이 겹쳐 보여 모든 문을 한 시점에서 볼 수는 없으므로 자유시점으로 층 내부에 접근하여 확인한다.
