# Agents Memory

## Project Requirements
- Language: C (C11 baseline)
- CLI entry: `./sql_processor <sql_file> [data_dir]`
- Read exactly one SQL statement from a file, parse it, and execute it later
- Project layout: `src/`, `include/`, `tests/`, `data/`, `queries/`, `Makefile`

## Required Interfaces
- `parse_sql(const char *sql, Statement *out, SqlError *err)`
- `execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err)`
- `storage_append_row(...)`

## MVP Grammar
- `INSERT INTO [schema.]table VALUES (...);`
- `SELECT * FROM [schema.]table;`
- Strings use single quotes
- Numbers are integers only
- No `WHERE`, no column list, no multiple statements
- Mixed-case keywords and flexible whitespace/newlines must be supported

## Storage Rules
- CSV only
- Header already exists
- `table` maps to `table.csv`
- `schema.table` maps to `schema__table.csv`
- Files live under `data/`

## Demo Data / Queries
- Demo CSV: `users.csv`
- Demo insert: `INSERT INTO users VALUES (1, 'alice', 20);`
- Demo select: `SELECT * FROM users;`

## Action Plan

### Phase 1 - CLI and Parser
- [x] Create project directories and `Makefile`
- [x] Implement CLI entrypoint and argument handling
- [x] Define `SqlError` and `Statement` in `include/`
- [x] Implement `parse_sql`
- [x] Add parser unit tests

### Phase 2 - Executor and Storage
- [ ] Implement storage module and `storage_append_row`
- [ ] Implement executor and `execute_statement`
- [ ] Integrate CLI -> Parser -> Executor -> Storage
- [ ] Add integration and edge case tests

## Current Status Checklist
- [x] Initial repository inspection completed
- [x] Phase plan drafted
- [x] Agents.md created and synced during implementation
- [x] Phase 1 implementation completed
- [x] Phase 1 review completed
- [x] Post-review structure refactor completed
- [ ] User approved Phase 2 start

## Architecture Decisions
- Phase 1 only for now. Do not implement Phase 2 behavior until the user explicitly approves moving on after review.
- Parser uses a manual scanner, not `strtok`.
- AST uses heap-owned strings and value arrays.
- Add `statement_init` and `statement_free` helper APIs for deterministic cleanup.
- Error reporting uses `int` return code with `out` and `err` output parameters.
- Baseline build target is WSL/Linux with `make`, but code should remain portable C11.
- `parse_sql` builds a temporary `Statement` and only transfers ownership to the caller on success.
- Phase 1 CLI reads the SQL file, parses it, and prints an AST summary instead of executing storage logic.
- String literals do not support quote escaping in MVP.
- Public headers are split by concern, while `include/sql_processor.h` remains as a convenience umbrella include.
- Phase 2 boundaries are compiled as explicit stubs so the link graph already reflects the intended architecture.

## Context Notes
- Current Windows shell does not have `gcc`, `clang`, or `make` on PATH.
- `cmake` exists, but the project contract remains `Makefile`.
- `git status` is currently blocked by Git safe-directory ownership settings, so avoid relying on git commands unless needed.
- `bash.exe` and `wsl.exe` access are currently blocked by service permission errors, so local `make test` verification could not be executed from this shell.
- Demo assets were created: `data/users.csv`, `queries/insert_users.sql`, `queries/select_users.sql`.
- Phase 1 rationale and implementation flow document added: `PHASE1_IMPLEMENTATION_NOTES.md`.
- `PHASE1_IMPLEMENTATION_NOTES.md` includes code review focus areas; some exact line mappings now predate the post-review refactor.

## Review Log
- Phase 1 code implemented: project skeleton, CLI, AST types, parser, and parser tests.
- Verification status: static review completed, compile/runtime verification blocked by missing compiler toolchain in the current shell.
- Documentation added: step-by-step implementation rationale and review notes in `PHASE1_IMPLEMENTATION_NOTES.md`.
- Documentation refined: review checkpoints now point to exact source files and line ranges.
- Post-review refactor applied: split headers by module and added `execute` / `storage` stub modules without changing Phase 1 scope.
- Pending: explain Phase 1 internals, review with the user, and wait for explicit approval before any Phase 2 code.
