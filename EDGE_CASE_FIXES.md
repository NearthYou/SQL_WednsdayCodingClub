# Edge Case Fixes

## 목적

이 문서는 Phase 3 구현에서 확인된 치명적인 에지 케이스와, 이번 수정에서 무엇을 어떻게 바꿨는지 기록한다.

## 수정한 문제

### 1. commit 중간 실패 시 원본 CSV가 깨질 수 있던 문제

기존 구현은 staged CSV를 원본 CSV로 반영할 때 대상 파일을 바로 `"wb"`로 열어 덮어썼다.
이 방식은 commit 도중 I/O 오류가 나면 원본 파일이 truncate 된 상태로 남을 수 있었다.

수정 내용:

- live CSV로 바로 copy 하지 않고, stage 안에 commit 임시 파일을 먼저 만든다.
- 기존 live CSV가 있으면 stage backup 파일로 먼저 이동한다.
- 준비된 commit 임시 파일을 마지막에 live CSV 위치로 `rename` 한다.
- commit 도중 실패하면 이미 반영한 CSV만 역순으로 rollback 한다.
- rollback 실패를 더 이상 무시하지 않고 에러로 올린다.

관련 파일:

- `src/execute.c`

### 2. 데이터 commit 후 최종 output 쓰기 실패 시 데이터만 남던 문제

기존 구현은 staged CSV commit이 끝난 뒤에야 buffered output을 실제 `out`으로 복사했다.
그래서 output write/flush 실패가 나면 함수는 실패를 반환하지만, CSV 변경은 이미 반영된 상태가 될 수 있었다.

수정 내용:

- commit 성공 후에도 backup 메타데이터를 유지한다.
- 최종 output 복사에 실패하면 committed CSV를 즉시 rollback 한다.
- rollback 성공 시 원래의 output 에러를 유지한다.
- rollback 자체가 실패하면 rollback 에러를 우선 보고한다.

관련 파일:

- `src/execute.c`

### 3. `SELECT *`가 깨진 CSV row를 그대로 통과시키던 문제

기존 구현은 `SELECT *`에서 파일 전체를 raw copy 했기 때문에,
header와 row column 수가 달라도 그대로 출력해 버렸다.
반면 projection SELECT는 row를 다시 파싱해서 검증하고 있었다.

수정 내용:

- `SELECT *`도 record 단위로 다시 읽는다.
- 각 row를 CSV parser로 검증한다.
- header column 수와 row column 수가 다르면 즉시 실패한다.
- 검증된 record만 출력한다.

관련 파일:

- `src/storage.c`

### 4. schema 기반 auto-create 실패 시 빈 CSV가 남을 수 있던 문제

기존 구현은 schema로 새 CSV를 만든 뒤,
이후 header reload / reopen / append 중 하나라도 실패하면 생성된 빈 CSV가 그대로 남을 수 있었다.

수정 내용:

- auto-create로 생성한 CSV는 `created_csv` 플래그로 추적한다.
- 생성 직후 이후 단계가 실패하면 방금 만든 CSV를 삭제한다.
- 즉, `storage_append_row()` 실패 후 흔적이 남지 않도록 정리한다.

관련 파일:

- `src/storage.c`

## 추가한 회귀 테스트

### `SELECT *` row shape 검증

- 깨진 row shape를 가진 CSV에 대해 `SELECT *`가 실패하는지 확인

### output 실패 시 rollback 검증

- read-only output stream에 결과를 쓰도록 유도
- `execute_script()`가 실패하면서도 CSV는 원래 상태로 rollback 되는지 확인

관련 파일:

- `tests/test_parser.c`

## 강화 검증

이번 수정 후 아래 매트릭스 회귀 테스트를 추가로 돌리도록 확장했다.

- staged rollback 실패 시나리오 50개
- `SELECT *` malformed CSV 거부 시나리오 50개
- `execute_script()` output 실패 rollback 시나리오 50개
- `execute_statement()` output 실패 rollback 시나리오 50개

즉, 기존 단건 회귀 테스트 외에 어려운 케이스 200개를 추가 검증 대상으로 넣었다.

## 이번 수정 범위 밖

- 임의의 `FILE *out`에 대해 "출력 자체를 완전히 원자적으로 보장"하는 것은 여전히 불가능하다.
- 예를 들어 OS가 output stream에 일부 바이트를 쓴 뒤 실패하면, 이미 써진 출력 바이트 자체를 되돌릴 방법은 없다.
- 이번 수정은 그 상황에서도 적어도 데이터 CSV는 rollback 되도록 만드는 데 초점을 맞췄다.
