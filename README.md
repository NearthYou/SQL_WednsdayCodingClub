# SQL_WednsdayCodingClub

Week 6 학습용 미니 SQL Processor 초안입니다.

## 현재 범위

- SQL 파일 하나에서 문장 하나만 읽습니다.
- 아래 두 문법만 지원합니다.
  - `INSERT INTO [schema.]table VALUES (...);`
  - `SELECT * FROM [schema.]table;`
- Phase 1에서는 파싱까지만 수행합니다.
- 실행기와 저장소 로직은 Phase 2용 스텁으로만 분리되어 있습니다.

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
  - Phase 2 실행기 공개 API를 정의합니다.
- `include/storage.h`
  - Phase 2 저장소 공개 API를 정의합니다.
- `src/parser.c`
  - 수동 스캐너 기반 SQL 파서를 구현합니다.
- `src/statement.c`
  - AST 초기화와 해제를 담당합니다.
- `src/sql_error.c`
  - 공통 에러 처리 함수를 구현합니다.
- `src/main.c`
  - CLI 진입점입니다. 파일을 읽고 파서를 호출하고 결과를 출력합니다.
- `src/execute_stub.c`
  - Phase 2 실행기 자리만 잡아둔 스텁입니다.
- `src/storage_stub.c`
  - Phase 2 저장소 자리만 잡아둔 스텁입니다.
- `tests/test_parser.c`
  - 파서 단위 테스트를 담고 있습니다.

## 왜 이렇게 나눴는가

- 파서를 학습 중심으로 읽기 쉽게 유지하기 위해서입니다.
- 실행기와 저장소는 아직 구현하지 않지만, 나중에 붙일 경계는 미리 분리해 두기 위해서입니다.
- `sql_processor.h`는 진입점을 단순하게 유지하고, 실제 책임은 개별 헤더가 나누어 갖도록 하기 위해서입니다.

## 빌드와 테스트

기본 빌드는 `Makefile` 기준입니다.

```bash
make clean all
make test
```

현재 Windows 로컬 환경에서는 경로 이슈 때문에 `mingw32-make`가 실패할 수 있어 direct `gcc` 빌드로 검증했습니다.

## CI/CD

- CI
  - GitHub Actions가 `push`와 `pull_request`에서 자동 실행됩니다.
  - Ubuntu에서는 `gcc`, `clang`으로 빌드와 테스트를 수행합니다.
  - Windows에서는 MSYS2 MinGW로 빌드와 테스트를 수행합니다.
  - 각 잡은 바이너리 빌드, parser 테스트, sample `INSERT` / `SELECT` 쿼리 실행까지 확인합니다.
- CD
  - `v0.1.0` 같은 태그를 푸시하면 Linux/Windows 릴리스 번들을 만들고 GitHub Release 자산으로 업로드합니다.
  - `workflow_dispatch`로 수동 릴리스 빌드도 가능합니다.
