# Agents Memory

## 프로젝트 개요

- 언어: C11
- CLI 진입점: `./sql_processor <sql_file> [data_dir]`
- 현재 기준 단계: Phase 3
- 기본 데이터 디렉터리: `data/`

## 현재 공개 인터페이스

- `parse_sql(const char *sql, Statement *out, SqlError *err)`
- `parse_sql_script(const char *sql, SqlScript *out, SqlError *err)`
- `execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err)`
- `execute_script(const SqlScript *script, const char *data_dir, FILE *out, SqlError *err)`
- `storage_append_row(...)`
- `storage_select_projection(...)`

## 현재 지원 문법

- `INSERT INTO [schema.]table VALUES (...);`
- `INSERT INTO [schema.]table (col1, col2, ...) VALUES (...);`
- `SELECT * FROM [schema.]table;`
- `SELECT col1, col2 FROM [schema.]table;`
- SQL 파일 하나에 여러 문장 배치 가능
- 문자열은 작은따옴표 사용
- 문자열 내부 작은따옴표는 `''`
- 정수 literal만 지원

## 현재 미지원 범위

- `WHERE`
- `NULL`
- default value
- 부분 컬럼 INSERT
- `UPDATE`, `DELETE`
- 정렬, 집계

## 저장소 규칙

- 데이터는 CSV 파일로 저장
- schema는 `.schema.csv` 파일로 저장
- 파일 매핑:
  - `table` -> `table.csv`, `table.schema.csv`
  - `schema.table` -> `schema__table.csv`, `schema__table.schema.csv`
- schema 타입:
  - `INT`
  - `STRING`

## Phase 3 실행 계약

- schema가 있으면 타입 검증 수행
- 명시적 컬럼 INSERT는 schema 순서로 재배열 후 저장
- 대상 CSV가 없더라도 schema가 있고 full column list INSERT면 자동 생성 가능
- `SELECT col1, col2`는 schema가 있을 때만 허용
- 여러 문장은 stage 디렉터리에서 먼저 실행
- 모든 문장이 성공해야 원본 CSV에 commit
- 출력도 성공 후 일괄 flush

## 학습 포인트

- `Statement.columns`, `select_all`, `SqlScript` 확장
- parser의 수동 스캐너 방식과 multi-statement 파싱
- storage의 schema-aware CSV 처리
- execute의 staging/rollback 구조

## 주요 파일

- `include/sql_types.h`
- `include/parse.h`
- `include/execute.h`
- `include/storage.h`
- `src/parser.c`
- `src/execute.c`
- `src/storage.c`
- `tests/test_parser.c`
- `README.md`
- `PHASE1_IMPLEMENTATION_NOTES.md`
- `PHASE2_IMPLEMENTATION_NOTES.md`
- `PHASE3_IMPLEMENTATION_NOTES.md`

## 샘플 자산

- `data/users.csv`
- `data/users.schema.csv`
- `queries/insert_users.sql`
- `queries/insert_users_with_columns.sql`
- `queries/select_users.sql`
- `queries/select_user_names.sql`
- `queries/script_users_roundtrip.sql`

## 검증 메모

- Windows Dev-Cpp MinGW `gcc.exe` 기준 직접 빌드/테스트 가능
- `mingw32-make`는 한글 경로 환경에서 실패할 수 있음
- 현재 회귀 테스트는 parser + Phase 3 execute 통합 테스트를 한 바이너리에서 수행함
