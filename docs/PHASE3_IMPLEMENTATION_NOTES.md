# Phase 3 구현 노트

## 목적

Phase 3의 목표는 Phase 2의 단일 CSV append/select 초안을 실제 학습용 mini DB에 더 가깝게 확장하는 것이다.

이번 단계에서 추가한 핵심 기능은 아래 6개다.

- CSV 파일 자동 생성
- 컬럼 이름 기반 매핑
- 타입 검증 강화
- 특정 컬럼만 조회
- 여러 SQL 문장 실행
- SQL 표준 `''` 문자열 escape 지원

## 1. 이번 단계에서 먼저 고정한 규칙

구현 전에 아래 규칙을 먼저 확정했다.

- schema 파일은 `.schema.csv` 형식으로 둔다.
- 지원 타입은 `INT`, `STRING` 두 가지만 쓴다.
- 새 기능은 schema가 있을 때만 적극적으로 활성화한다.
- 여러 문장은 전부 성공할 때만 반영한다.
- `SELECT` 결과도 중간에 바로 출력하지 않고 성공 후 일괄 출력한다.

이 규칙을 먼저 잠근 이유는, parser/execute/storage를 동시에 바꿔야 했기 때문이다.

## 2. AST를 어떻게 확장했는가

Phase 2의 `Statement`는 `schema`, `table`, `values` 정도만 들고 있었다.

Phase 3에서는 여기에 다음 의미를 추가했다.

- `columns`
  - `INSERT INTO table (col1, col2, ...)`의 컬럼 목록
  - `SELECT col1, col2 FROM table`의 projection 목록
- `column_count`
  - 컬럼 목록 길이
- `select_all`
  - `SELECT *`인지, 명시적 projection인지 구분

그리고 여러 문장 파일을 위해 `SqlScript`를 추가했다.

- `Statement *statements`
- `size_t statement_count`

즉, 단일 AST만 있던 구조를 “문장 목록 AST”까지 확장했다.

## 3. 왜 `parse_sql`을 없애지 않았는가

Phase 3에서도 `parse_sql`은 유지했다.

이유는 두 가지다.

- 이전 단계 학습 흐름과 호환성을 유지하기 쉽다.
- 단일 문장 테스트나 디버깅 진입점으로 여전히 유용하다.

대신 내부 구현은 `parse_sql_script`를 먼저 호출하고, 문장이 하나가 아니면 `parse_sql`이 실패하도록 바꿨다.

즉, 공개 API는 호환을 유지하고 실제 구현은 script 중심으로 이동했다.

## 4. 문자열 파서 결정

이번 단계에서 문자열 escape는 SQL 표준 방식만 추가했다.

- `''` -> 실제 `'`

백슬래시 escape는 넣지 않았다.

이 선택을 한 이유는 다음과 같다.

- 구현이 단순하다.
- SQL 문법과 더 가깝다.
- 학습자가 parser 상태 전이를 따라가기 쉽다.

또한 BOM이 붙은 UTF-8 SQL/schema/CSV 헤더도 읽을 수 있도록 보강했다.

## 5. schema 파일을 따로 둔 이유

CSV 헤더만 보고는 타입 검증을 할 수 없다.

그래서 별도 schema 파일을 아래 형식으로 고정했다.

```csv
name,type
id,INT
name,STRING
age,INT
```

이 형식을 택한 이유는 다음과 같다.

- 파싱이 쉽다.
- 사람이 직접 읽고 수정하기 쉽다.
- 나중에 타입이 늘어나도 행 단위 확장이 쉽다.

## 6. 컬럼 이름 기반 INSERT를 어떻게 처리했는가

명시적 컬럼 목록 INSERT는 입력 순서를 그대로 저장하지 않는다.

흐름은 아래와 같다.

1. schema 로드
2. 입력 컬럼 이름이 schema에 모두 존재하는지 확인
3. 중복 컬럼이 없는지 확인
4. schema 순서대로 값 배열을 재배열
5. 타입 검증
6. CSV row 직렬화 후 append

예를 들어 아래 SQL:

```sql
INSERT INTO users (name, id, age) VALUES ('alice', 1, 20);
```

실제 저장은 아래 순서로 간다.

```csv
1,"alice",20
```

즉, Phase 3 저장소는 “SQL 입력 순서”보다 “schema 순서”를 기준으로 동작한다.

## 7. CSV 자동 생성 규칙

CSV 자동 생성은 편하지만, 규칙이 느슨하면 데이터가 틀어진다.

그래서 아래 조건에서만 허용했다.

- 대상 CSV가 없음
- 대응 schema 파일이 존재함
- `INSERT`가 명시적 컬럼 목록을 사용함
- 컬럼 목록이 schema의 모든 컬럼을 정확히 한 번씩 포함함

이 제약을 둔 이유는 다음과 같다.

- header를 schema에서 안전하게 만들 수 있다.
- 컬럼 생략/default 같은 미지원 기능과 충돌하지 않는다.
- 학습자가 “자동 생성도 schema 기반 계약 위에서만 허용된다”는 점을 명확히 볼 수 있다.

## 8. projection SELECT 구현 포인트

Phase 2의 `SELECT`는 파일 전체를 그대로 흘려보냈다.

Phase 3의 `SELECT col1, col2 ...`는 schema 기준으로 동작한다.

흐름은 다음과 같다.

1. 대상 CSV와 schema 로드
2. header가 schema와 일치하는지 확인
3. 요청한 컬럼 이름을 schema index로 변환
4. 각 CSV row를 파싱
5. 선택한 필드만 다시 CSV로 직렬화해 출력

중요한 점은 projection 출력도 CSV 규칙을 다시 지킨다는 것이다.

- `STRING`은 항상 큰따옴표로 출력
- `INT`는 따옴표 없이 출력

## 9. 왜 staging 실행기를 넣었는가

여러 문장을 순서대로 실행할 때 가장 어려운 점은 rollback이다.

예를 들어:

```sql
INSERT ...
INSERT ... -- 실패
SELECT ...
```

이 경우 첫 번째 INSERT가 이미 원본 CSV를 바꿨다면 전체 실패를 보장할 수 없다.

그래서 실행기는 원본 `data_dir`를 바로 수정하지 않고, 임시 stage 디렉터리에서 먼저 실행한다.

흐름은 다음과 같다.

1. 필요한 table의 CSV/schema를 stage 디렉터리로 복사
2. 모든 문장을 stage에서 실행
3. 성공하면 stage의 변경 CSV만 원본으로 commit
4. 실패하면 stage를 버리고 종료

즉, “동작 확인은 stage에서, 실제 반영은 마지막에 한 번만” 하는 구조다.

## 10. 출력도 왜 버퍼링했는가

데이터만 rollback 되고 출력은 이미 콘솔에 찍혀 버리면 사용자는 성공처럼 오해할 수 있다.

그래서 `SELECT` 결과와 `INSERT 1 INTO ...` 메시지도 임시 출력 파일에 먼저 쓴다.

전체 성공 시에만 마지막에 `stdout`으로 복사한다.

이 덕분에 아래 계약이 성립한다.

- 실패한 script는 원본 데이터도 바꾸지 않는다.
- 실패한 script는 중간 출력도 남기지 않는다.

## 11. 저장소에서 신경 쓴 CSV 세부사항

이번 단계 저장소는 다음 세부사항을 직접 처리한다.

- UTF-8 BOM이 붙은 첫 필드 정리
- quoted CSV field 파싱
- `""` escape 처리
- 문자열은 항상 인용
- 파일 끝 개행 보정
- row 컬럼 수가 header와 다르면 실패

이 부분은 데이터가 조용히 깨지지 않게 하는 최소 안전장치다.

## 12. 테스트 전략

Phase 3 테스트는 parser와 execute를 같이 본다.

### parser 테스트

- INSERT column list
- SELECT projection
- SQL 표준 `''` escape
- BOM 입력
- 여러 문장 파싱
- 중복 identifier 실패

### execute 테스트

- schema 기준 컬럼 재정렬 INSERT
- schema 기반 CSV 자동 생성
- 타입 mismatch 실패
- projection SELECT
- INSERT 후 같은 script의 SELECT가 staged row를 보는지 확인
- script 중간 실패 시 rollback 확인

즉, 이번 단계 테스트는 “문법 확장”과 “실행 계약”을 동시에 잠그는 데 초점을 맞췄다.

## 13. 리뷰 포인트

Phase 3 리뷰에서는 아래를 우선해서 보면 좋다.

### parser

- column list와 projection이 기존 문법을 깨지 않는가
- 여러 문장 파싱이 세미콜론 경계를 안정적으로 처리하는가
- duplicate identifier를 조기에 막고 있는가

### execute

- stage 디렉터리 사용이 원본 오염 없이 동작하는가
- 성공 전 출력 버퍼링이 지켜지는가
- commit 실패 시 rollback 경로가 있는가

### storage

- schema가 있을 때와 없을 때 동작 경계가 명확한가
- auto-create 조건이 너무 느슨하지 않은가
- 타입 검증과 컬럼 재배열이 같은 schema 기준을 쓰는가
- projection SELECT가 header/row를 같은 index 기준으로 다루는가

## 14. 이번 단계에서 일부러 남겨 둔 범위

- `NULL`
- default value
- 부분 컬럼 INSERT
- `WHERE`
- 정렬, 집계
- schema 자동 생성
- 더 많은 타입(`FLOAT`, `BOOL`, `DATE` 등)

즉, Phase 3는 “schema-aware CSV mini DB”까지를 목표로 하고, 일반 SQL 엔진 수준 기능은 다음 단계로 남겨 두었다.
