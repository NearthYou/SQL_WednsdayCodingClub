# SQL_WednsdayCodingClub

Week 6 학습용 미니 SQL Processor입니다.  
현재 기준은 Phase 2까지 구현된 상태이며, SQL 파일 하나를 읽어 파싱한 뒤 CSV 파일에 실제로 반영하거나 조회합니다.

## 현재 지원 범위

- SQL 파일 하나에서 문장 하나만 읽습니다.
- 아래 두 문법만 지원합니다.
  - `INSERT INTO [schema.]table VALUES (...);`
  - `SELECT * FROM [schema.]table;`
- 키워드는 대소문자를 구분하지 않습니다.
- 공백과 개행은 유연하게 허용합니다.
- 문자열은 작은따옴표 리터럴만 지원합니다.
- 숫자는 정수만 지원합니다.

## Phase 2 동작

- `INSERT`
  - 대상 CSV 파일의 마지막에 새 행을 추가합니다.
  - 성공 시 `INSERT 1 INTO ...` 형식의 메시지를 출력합니다.
- `SELECT`
  - 대상 CSV 파일 전체를 헤더 포함 그대로 출력합니다.
- 기본 데이터 디렉터리는 `data/`입니다.
- CLI는 아래 형식으로 사용합니다.

```bash
./sql_processor <sql_file> [data_dir]
```

예시:

```bash
./sql_processor queries/insert_users.sql
./sql_processor queries/select_users.sql
```

## CSV 규칙

- 저장소는 CSV 파일만 사용합니다.
- CSV 파일은 미리 존재해야 합니다.
- 첫 줄은 헤더라고 가정합니다.
- 헤더 컬럼 수와 `INSERT` 값 개수가 정확히 같아야 합니다.
- 테이블 이름은 아래처럼 파일명으로 매핑합니다.
  - `table` -> `table.csv`
  - `schema.table` -> `schema__table.csv`
- 문자열 값은 저장할 때 항상 큰따옴표로 감쌉니다.
- 문자열 내부 큰따옴표는 `""`로 escape 합니다.

예시:

- SQL: `INSERT INTO users VALUES (1, 'alice', 20);`
- CSV 행: `1,"alice",20`

## 프로젝트 구조

- `include/sql_processor.h`
  - 전체 프로젝트를 한 번에 포함하는 우산 헤더입니다.
- `include/sql_common.h`
  - 공통 상수와 성공/실패 코드를 정의합니다.
- `include/sql_error.h`
  - 에러 코드, 에러 구조체, 에러 설정 함수를 정의합니다.
- `include/sql_types.h`
  - AST 역할을 하는 `Statement`, `SqlValue` 타입과 정리 함수를 정의합니다.
- `include/parse.h`
  - 파서 공개 API를 정의합니다.
- `include/execute.h`
  - AST를 실제 실행으로 연결하는 공개 API를 정의합니다.
- `include/storage.h`
  - CSV append를 담당하는 저장소 공개 API를 정의합니다.
- `src/parser.c`
  - 수동 스캐너 기반 SQL 파서를 구현합니다.
- `src/statement.c`
  - AST 초기화와 해제를 담당합니다.
- `src/sql_error.c`
  - 공통 에러 처리 함수를 구현합니다.
- `src/main.c`
  - CLI 진입점입니다. SQL 파일을 읽고 파싱한 뒤 실행기로 넘깁니다.
- `src/execute.c`
  - `INSERT` / `SELECT` 실행을 담당합니다.
- `src/storage.c`
  - CSV 파일 경로 계산, 헤더 검증, 행 append/select를 담당합니다.
- `tests/test_parser.c`
  - 파서 테스트와 Phase 2 통합 테스트를 함께 담고 있습니다.
- `queries/`
  - 샘플 SQL 파일을 담고 있습니다.
- `data/`
  - 샘플 CSV 데이터를 담고 있습니다.

## 아키텍처 의도

- 파서는 문법 해석만 담당합니다.
- 실행기는 `Statement`를 보고 어떤 동작을 할지 결정합니다.
- 저장소는 CSV 파일 경로 계산과 append 같은 파일 I/O 책임만 담당합니다.
- 이렇게 나누면 학습할 때도 `문법`, `실행`, `저장`을 분리해서 읽을 수 있습니다.

## 빌드와 테스트

기본 기준은 `Makefile`입니다.

```bash
make clean all
make test
```

현재 Windows 로컬 환경에서는 한글 경로 때문에 `mingw32-make`가 실패할 수 있어, 직접 `gcc`로도 검증했습니다.

검증 항목:

- `sql_processor.exe` 빌드
- `test_parser.exe` 빌드
- 파서 테스트
- Phase 2 실행 테스트
- sample `INSERT` / `SELECT` 실행

## CI/CD

- CI
  - GitHub Actions가 `push`와 `pull_request`에서 자동 실행됩니다.
  - Ubuntu에서는 `gcc`, `clang`으로 빌드와 테스트를 수행합니다.
  - Windows에서는 MSYS2 MinGW로 빌드와 테스트를 수행합니다.
  - 바이너리 빌드, 테스트 실행, sample `INSERT` / `SELECT` 실행까지 확인합니다.
- CD
  - `v0.1.0` 같은 태그를 푸시하면 Linux/Windows 릴리스 번들을 만들고 GitHub Release 자산으로 업로드합니다.
  - `workflow_dispatch`로 수동 릴리스 빌드도 가능합니다.

## 현재 의도적으로 제외한 기능

- `WHERE`
- 컬럼 목록 선택
- 다중 SQL 문장
- 문자열 escape를 포함한 더 넓은 SQL 문법
- CSV 스키마 자동 생성
- 타입 추론이나 컬럼 이름 기반 검증

## 학습 순서 추천

1. `include/sql_types.h`
2. `src/parser.c`
3. `src/main.c`
4. `src/execute.c`
5. `src/storage.c`
6. `tests/test_parser.c`

이 순서로 보면 Phase 1의 파싱 구조 위에 Phase 2 실행이 어떻게 얹히는지 자연스럽게 따라갈 수 있습니다.
