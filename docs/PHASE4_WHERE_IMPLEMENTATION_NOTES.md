# Phase 4 WHERE 구현 노트

## 목적

Phase 4의 목표는 기존 Phase 3의 schema-aware `SELECT` 흐름에 단일 조건 `WHERE`를 안전하게 추가하는 것이다.

이번 단계에서 추가한 핵심 기능은 아래와 같다.

- `SELECT ... WHERE column = literal`
- schema 기반 WHERE 컬럼/타입 검증
- raw / pretty 출력 모두 동일한 filtering 적용
- staging 실행 중 INSERT 뒤 WHERE SELECT 지원

## 1. 이번 단계에서 먼저 고정한 규칙

구현 전에 아래 규칙을 먼저 잠갔다.

- `WHERE`는 `SELECT`에만 추가한다.
- 지원 문법은 `WHERE column = literal` 한 가지다.
- 비교 literal은 기존 literal 규칙을 그대로 따라 `INT`, `STRING`만 허용한다.
- `WHERE`는 schema가 있는 테이블에서만 허용한다.
- `AND`, `OR`, `<`, `>`, `!=`, `LIKE`, `NULL`은 이번 단계에 넣지 않는다.

이 규칙을 먼저 고정한 이유는 parser/execute/storage 세 계층을 동시에 바꿔도 기존 계약을 흐리지 않기 위해서다.

## 2. AST를 어떻게 확장했는가

기존 `Statement`는 `SELECT`에 대해 아래 정보만 들고 있었다.

- projection 컬럼 목록
- `select_all`

Phase 4에서는 여기에 optional WHERE 조건을 추가했다.

- `has_where`
- `where_column`
- `where_value`

`where_value`는 기존 `SqlValue`를 그대로 재사용했다. 덕분에 parser가 새 literal 타입 체계를 따로 만들 필요가 없고, 메모리 소유권도 `Statement` 하나로 모을 수 있다.

## 3. parser에서 바뀐 점

`parse_select()` 흐름은 아래처럼 확장됐다.

1. `SELECT *` 또는 projection 파싱
2. `FROM schema.table` 또는 `FROM table` 파싱
3. optional `WHERE column = literal` 파싱

중요한 점은 `WHERE`를 optional suffix로만 붙였다는 것이다. 그래서 기존 `SELECT * FROM users;`, `SELECT name FROM users;`는 그대로 동작한다.

실패 규칙도 기존 스타일을 유지했다.

- column 누락: `Expected identifier`
- `=` 누락: `Expected '='`
- literal 누락: `Expected integer or string literal`

## 4. execute 계층에서 바뀐 점

실행기는 새 판단을 거의 하지 않는다.

- `execute_statement_unstaged()`가 `Statement`의 WHERE 정보를 storage로 그대로 전달한다.
- staging / buffered output / rollback 구조는 바꾸지 않는다.

즉, Phase 4에서도 핵심 계약은 그대로 유지된다.

- script 전체가 성공해야 CSV가 commit된다.
- script 전체가 성공해야 출력이 flush된다.
- `INSERT ...; SELECT ... WHERE ...;`는 stage 디렉터리의 최신 row를 본다.

## 5. storage에서 WHERE를 어떻게 처리했는가

실제 조건 평가는 storage에서 수행한다.

흐름은 아래와 같다.

1. schema 로드
2. WHERE가 있으면 schema 존재를 강제
3. WHERE 컬럼 이름을 schema index로 변환
4. literal 타입과 schema 타입이 일치하는지 확인
5. CSV row를 하나씩 파싱
6. WHERE 조건이 참인 row만 출력

### 왜 schema를 강제했는가

Phase 3의 projection도 schema 기준으로 동작한다. WHERE까지 header-only 규칙과 섞어 버리면 컬럼 존재 확인, 타입 비교, 에러 메시지 기준이 흐려진다.

그래서 이번 단계는 명확하게 아래처럼 고정했다.

- schema 없음 + WHERE 사용 -> 실패

### INT 비교는 어떻게 했는가

schema에서 WHERE 컬럼이 `INT`이면 row field를 다시 signed integer로 파싱해 literal과 비교한다.

이때 row 안에 숫자가 아닌 값이 있으면 조용히 무시하지 않고 parse error로 실패시킨다. 이유는 schema가 `INT`라고 선언된 테이블에서 잘못된 값이 섞여 있는 상태를 감추면 학습용 프로젝트 흐름이 오히려 불명확해지기 때문이다.

## 6. raw SELECT fast-path와 pretty 출력

Phase 3에는 `SELECT *` raw 출력일 때 CSV를 거의 그대로 흘려보내는 빠른 경로가 있었다.

Phase 4에서는 이 fast-path를 아래 조건에서만 유지했다.

- `SELECT *`
- raw 출력
- `WHERE` 없음

`WHERE`가 있으면 row를 실제로 파싱해서 조건을 검사해야 하므로 일반 filtering 경로로 내려간다.

pretty 출력도 같은 필터를 공유한다.

- width 측정 시 WHERE에 맞는 row만 본다.
- table render 시 WHERE에 맞는 row만 출력한다.
- 결과가 0건이면 header와 border만 남는다.

이렇게 맞춰야 raw와 pretty가 같은 결과 집합을 보여 준다.

## 7. 공개 인터페이스 변화

아래 공개 진입점은 그대로 유지했다.

- `parse_sql`
- `parse_sql_script`
- `execute_statement`
- `execute_script`

대신 내부 AST와 storage SELECT 인터페이스만 확장했다.

- `Statement`에 WHERE 필드 추가
- `storage_select_projection(...)`
- `storage_select_projection_mode(...)`

즉, 바깥에서 보는 top-level API는 유지하고 내부 SELECT 경로만 넓힌 셈이다.

## 8. 테스트 전략

이번 단계 테스트는 아래 네 축으로 구성했다.

### parser 성공

- `SELECT * FROM users WHERE id = 2;`
- `SELECT name FROM users WHERE name = 'alice';`

### parser 실패

- `WHERE` 뒤 column 누락
- `=` 누락
- literal 누락

### execute 성공

- raw `SELECT * WHERE`
- raw projection `SELECT ... WHERE`
- pretty `SELECT ... WHERE`
- no-match pretty result
- `INSERT ...; SELECT ... WHERE ...;`의 staged row 조회

### execute 실패

- schema 없는 WHERE
- 존재하지 않는 WHERE 컬럼
- schema 타입과 literal 타입 불일치
- INT schema 컬럼의 잘못된 row 값
- INSERT 후 WHERE 실패 시 rollback

## 9. 지원 문법과 아직 남은 범위

이번 단계 이후 지원하는 WHERE 문법은 아래 하나다.

```sql
SELECT col1, col2
FROM users
WHERE id = 2;
```

여전히 미지원인 범위는 아래와 같다.

- `AND`, `OR`
- `<`, `<=`, `>`, `>=`, `!=`
- `LIKE`
- `NULL`
- `UPDATE`, `DELETE`
- 정렬, 집계, JOIN

즉, Phase 4는 “schema-aware single-condition filtering”까지를 목표로 하고, 일반 SQL 조건식 엔진 수준 기능은 다음 단계로 남겨 둔다.

## 10. 읽기 흐름 요약

이번 단계 이후 프로젝트의 SELECT 흐름은 아래처럼 보면 된다.

1. parser가 projection과 optional WHERE를 `Statement`에 담는다.
2. execute가 script staging/buffering 계약을 유지한 채 storage로 전달한다.
3. storage가 schema를 기준으로 컬럼/타입을 검증한다.
4. CSV row를 읽으면서 WHERE를 평가하고 결과를 출력한다.

이 덕분에 기존 프로젝트의 흐름은 유지하면서도, 사용자는 이제 기본적인 조건 조회까지 한 번에 따라갈 수 있다.
