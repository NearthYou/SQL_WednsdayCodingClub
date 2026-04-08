<div align="center">
<img width="356" height="354" alt="image" src="https://github.com/user-attachments/assets/9662a483-849c-4ff2-9bfa-8ee0feaf7d9c" />

# SQL_WednsdayCodingClub

### CSV 기반 Mini SQL Processor

</div>

---

## 프로젝트 소개

우리 프로젝트는 SQL 파일을 읽어 CSV 데이터를 조회하고 반영하는 미니 SQL 처리기입니다.  
SQL 파일 안의 `INSERT`와 `SELECT` 문장을 파싱하고, schema 규칙에 맞게 CSV 데이터를 안전하게 조회하거나 반영하도록 구현했습니다.

현재 프로젝트는 실제 DB 엔진 전체를 구현하는 것이 아니라, 제한된 SQL 문법이 어떤 흐름으로 읽히고 실행되는지를 직접 확인할 수 있도록 만드는 데에 초점을 두었습니다.

---

## 핵심 설계

| 설계 포인트 | 선택한 방식 | 이유 |
|---|---|---|
| SQL 파싱 | **수동 스캐너 직접 구현** | 라이브러리를 쓰는 대신 괄호, 쉼표, 여러 문장 경계를 현재 지원 문법 안에서 일관되게 처리하기 위해 수동 스캐너 방식을 택했습니다. |
| 스키마 관리 | **테이블 CSV와 schema CSV 분리** | 컬럼 타입, 이름, 순서 같은 스키마 정보는 CSV 파일 하나만으로 관리하기 어려워 별도의 `schema.csv` 파일로 분리했습니다. |
| 다중 문장 실행 | **복사본에서 검증 후 commit** | 여러 SQL 문장을 실행할 때 원본 데이터를 바로 수정하면 중간에 하나만 실패해도 데이터가 애매하게 남을 수 있어, 복사본에서 끝까지 실행한 뒤 전부 성공했을 때만 실제 데이터에 반영하도록 만들었습니다. |

---

## 지원 기능

| 구분 | 내용 |
|---|---|
| `INSERT` | `INSERT INTO table VALUES (...);` |
| `INSERT` | `INSERT INTO table (col1, col2, ...) VALUES (...);` |
| `SELECT` | `SELECT * FROM table;` |
| `SELECT` | `SELECT col1, col2 FROM table;` |
| Multi-statement | SQL 파일 하나에 여러 문장 실행 가능 |
| Schema validation | 타입 검증, 컬럼 순서 재정렬, projection 검증 지원 |

---

## 시연 포인트

| 시연 내용 | 확인할 수 있는 점 |
|---|---|
| `INSERT INTO users (name, id, age) ...` | 입력 컬럼 순서가 달라도 schema 순서에 맞게 재정렬되어 저장됩니다. |
| `SELECT name, age FROM users;` | 필요한 컬럼만 projection 형태로 조회할 수 있습니다. |
| `INSERT ...; INSERT bad ...;` | 여러 문장 중 하나가 실패하면 전체 작업이 rollback되어 원본 데이터가 그대로 유지됩니다. |

---

## 협업 방식

저희는 코드 리뷰를 적극적으로 활용하여 협업을 진행했습니다. 먼저 명세서를 작성해 구현 범위와 동작을 정리한 뒤, Codex를 활용해 구현을 진행했습니다. 이후 4명이 모두 같은 화면을 보며 코드를 함께 분석하고 이해하는 시간을 가졌습니다.

이 과정을 통해 단순히 기능만 구현하는 데 그치지 않고, 실제 DB 명령어가 어떤 방식으로 파싱되고 실행되는지까지 같이 공부할 수 있었습니다.

---

## 한계

이 프로젝트는 학습 목적의 미니 SQL 처리기이므로 `WHERE`, `JOIN`, `UPDATE`, `DELETE`, 정렬, 집계 같은 고급 기능은 아직 지원하지 않습니다. 대신 현재 범위 안에서 파싱, schema 검증, 다중 문장 처리, rollback 흐름이 명확하게 드러나도록 구현했습니다.
