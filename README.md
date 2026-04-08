# SQL_WednsdayCodingClub

Mini SQL processor draft for Week 6.

## Current Scope

- Parse exactly one SQL statement from a file
- Support `INSERT INTO [schema.]table VALUES (...);`
- Support `SELECT * FROM [schema.]table;`
- Stop at parsing in Phase 1

## Project Layout

- `include/sql_processor.h`: umbrella header for the whole project
- `include/sql_common.h`: shared constants
- `include/sql_error.h`: error model and helpers
- `include/sql_types.h`: AST types and cleanup helpers
- `include/parse.h`: parser API
- `include/execute.h`: Phase 2 executor API
- `include/storage.h`: Phase 2 storage API
- `src/parser.c`: manual SQL scanner and parser
- `src/statement.c`: AST lifecycle helpers
- `src/sql_error.c`: shared error formatting helpers
- `src/execute_stub.c`: explicit Phase 2 placeholder
- `src/storage_stub.c`: explicit Phase 2 placeholder
- `tests/test_parser.c`: parser-focused unit tests

## Why This Shape

- The parser stays small and easy to trace for learning.
- Executor and storage are split early so Phase 2 work stays isolated.
- `sql_processor.h` remains as a simple entry point, while module headers make responsibilities explicit.

## CI/CD

- CI: GitHub Actions runs on push and pull request.
  - Ubuntu: `gcc`, `clang`
  - Windows: MinGW via MSYS2
  - Each job builds the project, runs parser tests, and executes the sample `INSERT` / `SELECT` queries.
- CD: pushing a tag like `v0.1.0` builds Linux and Windows release bundles and publishes them as GitHub Release assets.
- Manual release-asset verification is also available through `workflow_dispatch` on the `Release` workflow.
