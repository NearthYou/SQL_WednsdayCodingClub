# SQL_WednsdayCodingClub

Week 6 학습용 미니 SQL Processor입니다.  
현재 기준은 Phase 3까지 구현되어 있고, SQL 파일 하나에서 여러 문장을 읽어 CSV 파일에 반영하거나 조회할 수 있습니다.

## 핵심 포인트

- 파서는 수동 스캐너 방식입니다.
- 저장소는 데이터베이스 대신 `CSV + schema.csv`를 사용합니다.
- 실행기는 여러 문장을 `all-or-nothing` 규칙으로 실행합니다.
- `INSERT`가 먼저 실행된 뒤 같은 스크립트 안의 `SELECT`는 staged 결과를 볼 수 있습니다.

## 현재 지원 문법

- `INSERT INTO [schema.]table VALUES (...);`
- `INSERT INTO [schema.]table (col1, col2, ...) VALUES (...);`
- `SELECT * FROM [schema.]table;`
- `SELECT col1, col2 FROM [schema.]table;`
- SQL 파일 하나에 여러 문장을 순서대로 둘 수 있습니다.
- 키워드는 대소문자를 구분하지 않습니다.
- 공백과 개행은 유연하게 허용합니다.
- 문자열은 작은따옴표를 사용하고, 내부 작은따옴표는 `''`로 escape 합니다.
- 숫자는 정수만 지원합니다.

아직 지원하지 않는 것:

- `WHERE`
- `NULL`
- default value
- 일부 컬럼만 넣는 `INSERT`
- `UPDATE`, `DELETE`, `CREATE TABLE`

## CLI 사용법

```bash
./sql_processor <sql_file> [data_dir]
```

- `data_dir`를 생략하면 기본값은 `data/`입니다.
- 두 번째 인자는 `CSV 파일 경로`가 아니라 `데이터 디렉터리 경로`입니다.

예시:

```bash
./sql_processor queries/select_users.sql
./sql_processor queries/select_user_names.sql
./sql_processor queries/script_users_roundtrip.sql
```

## 파일 매핑 규칙

- `table` -> `table.csv`
- `schema.table` -> `schema__table.csv`
- schema 파일은 아래 규칙을 따릅니다.
  - `table` -> `table.schema.csv`
  - `schema.table` -> `schema__table.schema.csv`

예시:

- `users` -> `users.csv`, `users.schema.csv`
- `app.users` -> `app__users.csv`, `app__users.schema.csv`

## schema 파일 형식

schema 파일은 단순 CSV 형식입니다.

```csv
name,type
id,INT
name,STRING
age,INT
```

현재 지원 타입:

- `INT`
- `STRING`

## 실행 규칙

### 1. CSV 자동 생성

대상 CSV가 없더라도 아래 조건이면 자동 생성됩니다.

- 대응하는 `.schema.csv` 파일이 존재해야 합니다.
- `INSERT`가 명시적 컬럼 목록을 사용해야 합니다.
- 컬럼 목록이 schema의 모든 컬럼을 정확히 한 번씩 포함해야 합니다.

예시:

```sql
INSERT INTO users (id, name, age) VALUES (1, 'alice', 20);
```

### 2. 컬럼 이름 기반 매핑

명시적 컬럼 목록이 있으면 schema 기준 순서로 재정렬해서 저장합니다.

```sql
INSERT INTO users (name, id, age) VALUES ('alice', 1, 20);
```

위 SQL도 실제 CSV에는 아래처럼 저장됩니다.

```csv
id,name,age
1,"alice",20
```

### 3. 타입 검증

schema 파일이 있으면 컬럼별 타입을 검증합니다.

- `INT` 컬럼에는 정수만 허용
- `STRING` 컬럼에는 문자열만 허용

타입이 맞지 않으면 실행 전체가 실패합니다.

### 4. 특정 컬럼만 조회

schema 파일이 있으면 projection SELECT를 지원합니다.

```sql
SELECT name, age FROM users;
```

출력도 선택한 컬럼만 포함합니다.

```csv
name,age
"alice",20
```

### 5. 여러 SQL 문장 실행

SQL 파일 하나에 여러 문장을 쓸 수 있습니다.

```sql
INSERT INTO users (id, name, age) VALUES (2, 'bob', 30);
SELECT name, age FROM users;
```

실행 규칙:

- 모든 문장이 성공해야 실제 CSV가 변경됩니다.
- 중간에 하나라도 실패하면 전체를 rollback 합니다.
- `SELECT` 출력과 `INSERT` 성공 메시지도 전체 성공 후에만 출력합니다.

## CSV 저장 규칙

- 첫 줄은 헤더입니다.
- 문자열은 항상 큰따옴표로 저장합니다.
- 문자열 내부 큰따옴표는 `""`로 escape 합니다.
- 정수는 따옴표 없이 저장합니다.

예시:

- SQL: `INSERT INTO users VALUES (1, 'a,b"c', 20);`
- CSV row: `1,"a,b""c",20`

## 샘플 파일

- `data/users.csv`
  - 기본 데모 데이터
- `data/users.schema.csv`
  - `users.csv`용 schema
- `queries/insert_users.sql`
  - 기존 positional INSERT 예제
- `queries/insert_users_with_columns.sql`
  - 컬럼 이름 기반 INSERT 예제
- `queries/select_users.sql`
  - `SELECT *` 예제
- `queries/select_user_names.sql`
  - projection SELECT 예제
- `queries/script_users_roundtrip.sql`
  - multi-statement 예제

## 프로젝트 구조

- `include/sql_processor.h`
  - 우산 헤더
- `include/sql_types.h`
  - `Statement`, `SqlScript`, `SqlValue`
- `include/parse.h`
  - `parse_sql`, `parse_sql_script`
- `include/execute.h`
  - `execute_statement`, `execute_script`
- `include/storage.h`
  - schema-aware CSV 저장/조회 API
- `src/parser.c`
  - 수동 스캐너 기반 SQL 파서
- `src/statement.c`
  - AST 초기화/정리
- `src/execute.c`
  - multi-statement 실행, staging, rollback
- `src/storage.c`
  - CSV/schema 읽기, 타입 검증, projection SELECT
- `tests/test_parser.c`
  - 파서 + Phase 3 실행 통합 테스트

## 빌드와 테스트

기본 기준은 `Makefile`입니다.

```bash
make clean all
make test
```

현재 Windows 로컬 환경에서는 한글 경로 때문에 `mingw32-make`보다 `gcc` 직접 호출이 더 안정적일 수 있습니다.

직접 빌드 예시:

```powershell
& "C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\gcc.exe" -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g -o sql_processor.exe src\main.c src\parser.c src\statement.c src\sql_error.c src\execute.c src\storage.c
& "C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\gcc.exe" -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g -o test_parser.exe tests\test_parser.c src\parser.c src\statement.c src\sql_error.c src\execute.c src\storage.c
.\test_parser.exe
```

## CI/CD

- CI
  - GitHub Actions가 `push`와 `pull_request`에서 빌드와 테스트를 수행합니다.
- CD
  - `v*` 태그를 푸시하면 릴리스 번들을 생성합니다.

## 학습 순서 추천

1. `include/sql_types.h`
2. `src/parser.c`
3. `src/statement.c`
4. `src/main.c`
5. `src/execute.c`
6. `src/storage.c`
7. `tests/test_parser.c`

Phase 3에서는 특히 아래 질문을 붙여서 보면 좋습니다.

- 컬럼 이름 INSERT가 schema 순서로 어떻게 재배열되는가
- projection SELECT가 어떤 기준으로 컬럼을 고르는가
- 여러 문장을 실행할 때 왜 staging이 필요한가
- rollback이 어느 시점에 보장되는가
