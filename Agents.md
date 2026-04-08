# Agents Memory

## 프로젝트 요구사항

- 언어: C (C11 기준)
- CLI 진입점: `./sql_processor <sql_file> [data_dir]`
- SQL 파일에서 문장 하나를 읽고 파싱한 뒤, 이후 단계에서 실행할 수 있어야 함
- 프로젝트 구조: `src/`, `include/`, `tests/`, `data/`, `queries/`, `Makefile`

## 필수 인터페이스

- `parse_sql(const char *sql, Statement *out, SqlError *err)`
- `execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err)`
- `storage_append_row(...)`

## MVP 문법

- `INSERT INTO [schema.]table VALUES (...);`
- `SELECT * FROM [schema.]table;`
- 문자열은 작은따옴표 사용
- 숫자는 정수만 지원
- `WHERE`, 컬럼 목록, 다중 문장은 지원하지 않음
- 키워드는 대소문자를 구분하지 않고, 공백과 개행을 유연하게 허용해야 함

## 저장소 규칙

- CSV만 사용
- 헤더는 이미 존재한다고 가정
- `table`은 `table.csv`로 매핑
- `schema.table`은 `schema__table.csv`로 매핑
- 파일은 `data/` 아래에 위치

## 데모 데이터 / 쿼리

- 데모 CSV: `users.csv`
- 데모 INSERT: `INSERT INTO users VALUES (1, 'alice', 20);`
- 데모 SELECT: `SELECT * FROM users;`

## 작업 계획

### Phase 1 - CLI와 파서

- [x] 프로젝트 디렉터리와 `Makefile` 생성
- [x] CLI 진입점과 인자 처리 구현
- [x] `include/`에 `SqlError`, `Statement` 정의
- [x] `parse_sql` 구현
- [x] parser 단위 테스트 추가

### Phase 2 - 실행기와 저장소

- [ ] 저장소 모듈과 `storage_append_row` 구현
- [ ] 실행기와 `execute_statement` 구현
- [ ] CLI -> Parser -> Executor -> Storage 통합
- [ ] 통합 테스트와 경계 케이스 테스트 추가

## 현재 상태 체크리스트

- [x] 초기 저장소 확인 완료
- [x] 단계별 계획 작성 완료
- [x] 구현 중 `Agents.md` 생성 및 동기화 완료
- [x] Phase 1 구현 완료
- [x] Phase 1 리뷰 완료
- [x] 리뷰 후 구조 리팩터링 완료
- [ ] 사용자 승인 후 Phase 2 시작 예정

## 아키텍처 결정

- 지금은 Phase 1까지만 다룸. 사용자가 명시적으로 승인하기 전에는 Phase 2 동작을 구현하지 않음.
- 파서는 `strtok` 대신 수동 스캐너를 사용함.
- AST는 힙에 할당된 문자열과 값 배열을 사용함.
- 예측 가능한 정리를 위해 `statement_init`, `statement_free`를 제공함.
- 에러는 `int` 반환값과 `out` / `err` 출력 파라미터 조합으로 전달함.
- 기본 빌드 기준은 WSL/Linux `make`지만, 코드는 가능한 한 portable C11로 유지함.
- `parse_sql`은 임시 `Statement`를 먼저 만든 뒤 성공 시에만 호출자에게 소유권을 넘김.
- Phase 1 CLI는 파일을 읽고 파싱 결과 요약만 출력하며, 저장소 실행은 하지 않음.
- MVP에서는 문자열 escape를 지원하지 않음.
- 공개 헤더는 역할별로 나누고, `include/sql_processor.h`는 편의용 우산 헤더로 유지함.
- Phase 2 경계는 실제 링크 구조에 드러나도록 스텁 모듈로 분리해 둠.

## 컨텍스트 메모

- 현재 Windows 셸에는 `gcc`, `clang`, `make`가 PATH에 없음.
- `cmake`는 있지만 프로젝트 계약은 여전히 `Makefile` 기준임.
- 과거에는 `git safe.directory` 설정이 없어 Git 명령이 막혔음.
- `bash.exe`, `wsl.exe`는 권한 문제로 로컬 `make test` 검증에 바로 쓰기 어려웠음.
- 데모 자산으로 `data/users.csv`, `queries/insert_users.sql`, `queries/select_users.sql`를 추가함.
- Phase 1 구현 배경과 리뷰 포인트는 `PHASE1_IMPLEMENTATION_NOTES.md`에 정리함.
- `PHASE1_IMPLEMENTATION_NOTES.md`의 일부 라인 번호 기반 설명은 리팩터링 이전 기준일 수 있음.

## 리뷰 로그

- Phase 1 코드 구현 완료: 프로젝트 뼈대, CLI, AST 타입, parser, parser 테스트
- 정적 리뷰 완료
- 로컬 빌드/실행 검증은 툴체인 제약 때문에 한동안 보류됐지만, 이후 Windows Dev-Cpp MinGW 기준 직접 검증 완료
- `PHASE1_IMPLEMENTATION_NOTES.md`에 구현 배경과 리뷰 포인트 문서화 완료
- 리뷰 후 헤더 역할 분리, `execute` / `storage` 스텁 모듈 추가 완료
- 현재 PR 브랜치에는 GitHub Actions 기반 CI/CD도 추가됨
