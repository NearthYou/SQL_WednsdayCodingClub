# CLI 시연 시나리오

이 문서는 `sql_processor.exe`를 직접 실행하면서 프로젝트의 핵심 기능을 순서대로 보여주기 위한 시연 가이드입니다.  
기준은 Windows PowerShell이며, 프로젝트 루트에서 실행하는 것을 전제로 합니다.

## 0. 목표

이 시연으로 아래 기능을 눈으로 확인할 수 있습니다.

- `SELECT *`
- 컬럼 이름 기반 `INSERT`
- 특정 컬럼만 조회
- 여러 SQL 문장 실행
- 실패 시 rollback
- schema 기반 CSV 자동 생성

## 1. 시작 전 준비

프로젝트 루트로 이동합니다.

```powershell
cd "C:\Users\fhrhd\바탕 화면\Jungle\Week06\Codex\SQL_WednsdayCodingClub"
```

빌드가 아직 안 되어 있으면 아래 명령으로 빌드합니다.

```powershell
& "C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\gcc.exe" -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g -o sql_processor.exe src\main.c src\parser.c src\statement.c src\sql_error.c src\execute.c src\storage.c
& "C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\gcc.exe" -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g -o test_parser.exe tests\test_parser.c src\parser.c src\statement.c src\sql_error.c src\execute.c src\storage.c
.\test_parser.exe
```

## 2. 시연용 데이터 폴더 만들기

tracked `data/`를 직접 바꾸지 않도록 별도 폴더를 만듭니다.

```powershell
New-Item -ItemType Directory -Force .\demo_data | Out-Null
Copy-Item .\data\users.csv .\demo_data\users.csv -Force
Copy-Item .\data\users.schema.csv .\demo_data\users.schema.csv -Force
```

현재 데이터 확인:

```powershell
Get-Content .\demo_data\users.csv
```

예상 초기 상태:

```text
id,name,age
1,"alice",20
```

## 3. 시나리오 1: 기본 조회

가장 먼저 `SELECT *`가 되는지 확인합니다.

```powershell
.\sql_processor.exe .\queries\select_users.sql .\demo_data
```

예상 출력:

```text
id,name,age
1,"alice",20
```

### 여기서 보여줄 포인트

- 두 번째 인자는 `CSV 파일 경로`가 아니라 `데이터 디렉터리`입니다.
- 현재 프로젝트는 CSV를 DB처럼 읽어 와서 그대로 출력합니다.

## 4. 시나리오 2: 컬럼 이름 기반 INSERT

다음은 컬럼 순서를 바꿔서 넣어도 schema 기준으로 맞춰 저장되는지 보여줍니다.

```powershell
.\sql_processor.exe .\queries\insert_users_with_columns.sql .\demo_data
Get-Content .\demo_data\users.csv
```

예상 출력:

```text
INSERT 1 INTO users
```

예상 CSV:

```text
id,name,age
1,"alice",20
2,"bob",30
```

### 여기서 보여줄 포인트

- SQL은 `name, id, age` 순서로 들어오지만
- 실제 저장은 schema 순서인 `id, name, age`로 맞춰집니다.

## 5. 시나리오 3: 특정 컬럼만 조회

이제 projection SELECT를 실행합니다.

```powershell
.\sql_processor.exe .\queries\select_user_names.sql .\demo_data
```

예상 출력:

```text
name,age
"alice",20
"bob",30
```

### 여기서 보여줄 포인트

- `SELECT *`가 아니라 `SELECT name, age`만 출력합니다.
- 출력 헤더도 선택한 컬럼만 나옵니다.

## 6. 시나리오 4: 여러 SQL 문장 실행

한 SQL 파일 안에 `INSERT`와 `SELECT`를 같이 넣은 예제를 실행합니다.

```powershell
New-Item -ItemType Directory -Force .\demo_script_data | Out-Null
Copy-Item .\data\users.csv .\demo_script_data\users.csv -Force
Copy-Item .\data\users.schema.csv .\demo_script_data\users.schema.csv -Force

.\sql_processor.exe .\queries\script_users_roundtrip.sql .\demo_script_data
Get-Content .\demo_script_data\users.csv
```

예상 출력:

```text
INSERT 1 INTO users
name,age
"alice",20
"bob",30
```

예상 CSV:

```text
id,name,age
1,"alice",20
2,"bob",30
```

### 여기서 보여줄 포인트

- 같은 script 안에서 앞선 `INSERT` 결과를 뒤의 `SELECT`가 볼 수 있습니다.
- 하지만 실제 반영은 마지막에 한 번만 commit 됩니다.

## 7. 시나리오 5: 실패 시 rollback

이번에는 일부러 두 번째 문장을 실패시켜서, 첫 번째 `INSERT`도 실제 반영되지 않는지 보여줍니다.

실패용 SQL 파일 생성:

```powershell
@"
INSERT INTO users (id, name, age) VALUES (2, 'bob', 30);
INSERT INTO users VALUES ('bad', 'oops', 10);
SELECT * FROM users;
"@ | Set-Content -Encoding utf8 .\rollback_demo.sql
```

실행:

```powershell
New-Item -ItemType Directory -Force .\demo_rollback_data | Out-Null
Copy-Item .\data\users.csv .\demo_rollback_data\users.csv -Force
Copy-Item .\data\users.schema.csv .\demo_rollback_data\users.schema.csv -Force

.\sql_processor.exe .\rollback_demo.sql .\demo_rollback_data
Get-Content .\demo_rollback_data\users.csv
```

예상 결과:

- CLI는 `Execution error [...]`를 출력합니다.
- CSV는 그대로 유지됩니다.

예상 CSV:

```text
id,name,age
1,"alice",20
```

### 여기서 보여줄 포인트

- 두 번째 `INSERT`가 타입 검증에서 실패합니다.
- 첫 번째 `INSERT`도 rollback 되어 원본 데이터가 바뀌지 않습니다.
- 출력도 부분 성공 상태로 남기지 않습니다.

## 8. 시나리오 6: CSV 자동 생성

CSV 파일이 없어도 schema가 있으면 생성되는지 보여줍니다.

먼저 새 폴더 준비:

```powershell
New-Item -ItemType Directory -Force .\demo_autocreate_data | Out-Null
Copy-Item .\data\users.schema.csv .\demo_autocreate_data\users.schema.csv -Force
```

실행:

```powershell
.\sql_processor.exe .\queries\insert_users_with_columns.sql .\demo_autocreate_data
Get-Content .\demo_autocreate_data\users.csv
```

예상 출력:

```text
INSERT 1 INTO users
```

예상 CSV:

```text
id,name,age
2,"bob",30
```

### 여기서 보여줄 포인트

- `users.csv`가 없어도
- `users.schema.csv`가 있고
- full column list INSERT면 자동 생성됩니다.

## 9. 시나리오 7: 문자열 escape

SQL 문자열 안의 작은따옴표 escape도 확인할 수 있습니다.

```powershell
@"
INSERT INTO users VALUES (3, 'it''s fine', 40);
"@ | Set-Content -Encoding utf8 .\string_escape_demo.sql

New-Item -ItemType Directory -Force .\demo_escape_data | Out-Null
Copy-Item .\data\users.csv .\demo_escape_data\users.csv -Force
Copy-Item .\data\users.schema.csv .\demo_escape_data\users.schema.csv -Force

.\sql_processor.exe .\string_escape_demo.sql .\demo_escape_data
Get-Content .\demo_escape_data\users.csv
```

예상 마지막 줄:

```text
3,"it's fine",40
```

## 10. 시연 마무리 때 강조하면 좋은 포인트

- 이 프로젝트는 단순 parser가 아니라 `parse -> execute -> storage` 구조를 가진 mini SQL processor입니다.
- Phase 3 기준으로는 schema 기반 타입 검증과 multi-statement rollback까지 지원합니다.
- 실제 DB 엔진은 아니지만, AST, staging, rollback, projection 같은 핵심 개념을 학습하기 좋게 구현했습니다.

## 11. 자주 실수하는 부분

- 두 번째 인자에 `data\users.csv` 같은 파일 경로를 넣으면 안 됩니다.
- 반드시 `data` 또는 `demo_data` 같은 디렉터리를 넣어야 합니다.
- `SELECT name, age ...`는 schema 파일이 없으면 실패합니다.
- CSV 자동 생성은 아무 `INSERT`나 되는 게 아니라, schema + full column list가 필요합니다.

## 12. 빠른 데모 루트

시간이 3분 정도밖에 없으면 아래 4개만 시연해도 됩니다.

1. `SELECT *`
2. 컬럼 이름 기반 `INSERT`
3. 특정 컬럼 `SELECT`
4. rollback 실패 시나리오

추천 실행 순서:

```powershell
.\sql_processor.exe .\queries\select_users.sql .\demo_data
.\sql_processor.exe .\queries\insert_users_with_columns.sql .\demo_data
.\sql_processor.exe .\queries\select_user_names.sql .\demo_data
.\sql_processor.exe .\rollback_demo.sql .\demo_rollback_data
```
