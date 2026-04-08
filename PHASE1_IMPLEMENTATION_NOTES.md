# Phase 1 Implementation Notes

## Purpose
- This document does not expose a raw internal thought log.
- It records the shareable engineering rationale, implementation order, and review points for Phase 1.
- Scope is limited to CLI setup and SQL parsing. Executor and storage are intentionally excluded.

## Phase 1 Goal
- Read one SQL file.
- Accept exactly one SQL statement per file.
- Support:
  - `INSERT INTO [schema.]table VALUES (...);`
  - `SELECT * FROM [schema.]table;`
- Convert the SQL text into a `Statement` AST.
- Stop at parsing in Phase 1. Do not execute storage logic yet.

## 1. Initial Grounding
- Confirmed the repository was nearly empty.
- Created `Agents.md` first and used it as persistent project memory.
- From that point on, the working rule became:
  - read `Agents.md` before major work,
  - update it after design decisions or task completion.

## 2. Environment Facts Found First
- The current PowerShell environment does not have `gcc`, `clang`, or `make` on PATH.
- `cmake` exists, but the project contract is still `Makefile`.
- `bash.exe` and `wsl.exe` are blocked by permission errors in this shell.
- Result:
  - code was written to portable C11 assumptions,
  - runtime compile verification could not be completed from this shell,
  - this status was recorded in `Agents.md`.

## 3. Why the Implementation Started with Headers
- The user already fixed the public interfaces.
- Because of that, the safest order was:
  1. define shared types and function declarations,
  2. define ownership and cleanup rules,
  3. build the parser on top of those rules,
  4. build the CLI around the parser,
  5. add tests around the parser contract.

## 4. Shared Type Design

### `SqlError`
- C has no exception system.
- A plain integer return value is not enough for useful debugging.
- `SqlError` stores:
  - `code`
  - `position`
  - `message`
- This makes parser failures observable and reviewable.

### `Statement`
- MVP only needs two statement kinds:
  - `STMT_INSERT`
  - `STMT_SELECT`
- Common fields:
  - `type`
  - `schema`
  - `table`
- Insert-specific payload:
  - `values`
  - `value_count`
- `SELECT *` needs no column list in Phase 1.

### `SqlValue`
- Values are either integers or strings.
- A tagged union was chosen:
  - `type` tells which variant is active,
  - the union stores the actual payload.
- This keeps the representation compact and reusable for Phase 2 CSV output.

## 5. Memory Ownership Strategy
- The first critical C design question was ownership.
- The chosen rule is heap-owned AST data:
  - parser deep-copies identifiers and string literals,
  - parser allocates the `values` array,
  - caller owns the successful `Statement`,
  - caller must release it with `statement_free`.

### Why this strategy
- AST lifetime is decoupled from the input SQL buffer lifetime.
- The CLI can free the original file buffer without invalidating the AST.
- Tests stay simple because they do not depend on input string lifetime.

### Success and failure behavior
- On success:
  - `out` receives ownership of all heap data.
- On failure:
  - the parser frees all partially built allocations before returning.
- This keeps the caller from dealing with partially initialized cleanup.

## 6. Why a Manual Scanner Was Chosen Instead of `strtok`
- SQL parsing needs context-sensitive control over:
  - whitespace,
  - punctuation,
  - keywords,
  - quoted strings,
  - exact error position.
- `strtok` was rejected because it:
  - mutates the input buffer,
  - makes position tracking harder,
  - handles quoted strings poorly,
  - is awkward for tokens like `.`, `(`, `)`, `,`, `;`.
- A manual scanner was used instead:
  - `Parser { input, length, pos }`
  - helper functions inspect and advance one region at a time.

## 7. Parser Construction Order

### 7-1. Minimal scanner helpers
- `current_char`
- `skip_whitespace`
- `copy_substring`
- `match_keyword`
- `expect_keyword`

These functions define the basic "read at current position, then advance" model.

### 7-2. Identifier parsing
- `parse_identifier` accepts `[A-Za-z_]` followed by `[A-Za-z0-9_]*`.
- `parse_qualified_name` reads either:
  - `table`
  - `schema.table`

### 7-3. Literal parsing
- Integer literals use `strtoll`.
- String literals copy the content between single quotes.
- Quote escaping is intentionally not supported in MVP.

### 7-4. VALUES list parsing
- Confirm `(`.
- Parse one value.
- Repeatedly accept either:
  - `,` and another value,
  - `)` to finish.
- Empty `()` is rejected in MVP.

### 7-5. Statement parsing
- `INSERT` path:
  - `INTO`
  - target table
  - `VALUES`
  - values list
- `SELECT` path:
  - `*`
  - `FROM`
  - target table

### 7-6. Statement termination
- Require a final `;`.
- Allow only trailing whitespace after `;`.
- This enforces the "one statement per file" rule.

## 8. Why `parse_sql` Uses a Temporary Local `Statement`
- `parse_sql` builds a local `Statement stmt` first.
- If parsing fails halfway, that local object can be cleaned safely.
- Only on success does the function transfer it with `*out = stmt`.
- This avoids leaving the caller with a half-built object.

## 9. CLI Construction
- `main.c` was intentionally kept small in Phase 1.
- Execution order:
  1. validate CLI arguments,
  2. read the SQL file into memory,
  3. call `parse_sql`,
  4. print a normalized AST summary on success,
  5. print code, position, and message on failure.
- `execute_statement` is not called yet by design.

## 10. Test Construction
- Tests were written as a single C executable without external frameworks.
- Reasons:
  - minimal dependency surface,
  - easy to compile in a basic C environment,
  - each test also documents one parser behavior.

### Success cases added
- simple insert
- simple select
- qualified schema name
- mixed case keywords
- extra whitespace and newlines
- negative integer

### Failure cases added
- missing semicolon
- column list in select
- unterminated string
- multiple statements
- where clause usage

## 11. Ongoing Design Rules During Implementation
- Keep Phase 1 focused on parsing only.
- Keep ownership explicit.
- Record error positions as close to the actual fault as possible.
- Do not silently expand beyond the MVP grammar.
- Do not mix executor or storage behavior into the parser phase.

## 12. Intentional Non-Features in Phase 1
- no string escape support
- no floating point numbers
- no `WHERE`
- no column selection
- no multiple statements
- no CSV read/write execution
- no implementation yet for:
  - `execute_statement`
  - `storage_append_row`

## 13. Review Questions to Confirm Understanding
- Why is the manual scanner a better fit than `strtok` here?
- Who owns `Statement` memory after `parse_sql` succeeds?
- Why is a local temporary `Statement` safer than writing directly into `out` from the start?
- Why is `err.position` valuable during debugging?

## 14. Current Status
- Phase 1 implementation is complete.
- Phase 2 has not started.
- `Agents.md` has been updated with current state.
- Compile and runtime verification are still pending because the current shell lacks a usable compiler toolchain.

## 15. Post-review Structure Refactor
- The parser remains the teaching center of the project.
- Public headers are now split by role:
  - shared constants
  - error handling
  - AST types
  - parser API
  - executor API
  - storage API
- `include/sql_processor.h` remains as a convenience umbrella include so entry-level readers still have a simple starting point.
- `execute_statement` and `storage_append_row` now exist as explicit stub modules.
- This keeps the Phase 1 code easy to learn while making the Phase 2 extension points visible in the actual build graph.
- Some earlier line-by-line review references in this document describe the pre-refactor layout and should be refreshed after the next compile-capable review pass.

## 15. Code Review Focus Map

### 15-1. Public contract and type boundaries
- Check `include/sql_processor.h:11-66`.
- Review points:
  - whether `SqlError` carries enough information for parser and executor stages,
  - whether `Statement` is minimal but sufficient for MVP,
  - whether `SqlValue` is the right tagged-union shape for later CSV serialization,
  - whether helper APIs like `statement_init` and `statement_free` are correctly exposed.

### 15-2. AST initialization and cleanup discipline
- Check `src/statement.c:6-32`.
- Review points:
  - whether every heap-owned field is released,
  - whether cleanup is safe when only part of the AST was initialized,
  - whether `statement_init` after free prevents stale pointers and double-free risk.

### 15-3. Error reporting pattern
- Check `src/parser.c:16-39`.
- Review points:
  - whether `clear_error` and `set_error` are called consistently,
  - whether the `position` recorded is close enough to the real fault,
  - whether error codes distinguish lexical, parse, unsupported, and memory failures clearly enough.

### 15-4. Scanner movement and keyword matching
- Check `src/parser.c:57-123`.
- Review points:
  - whether `current_char` handles end-of-buffer safely,
  - whether `skip_whitespace` is applied at the right times,
  - whether `match_keyword` correctly rejects prefixes such as `SELECTED`,
  - whether case-insensitive matching is limited to ASCII in an intentional way.

### 15-5. Identifier parsing and qualified name parsing
- Check `src/parser.c:126-180`.
- Review points:
  - whether identifier syntax matches the intended grammar,
  - whether `schema.table` and plain `table` are both handled cleanly,
  - whether memory allocated for the first identifier is freed correctly if parsing the second identifier fails.

### 15-6. Integer and string literal parsing
- Check `src/parser.c:194-267`.
- Review points:
  - whether signed integers are accepted correctly,
  - whether `strtoll` failure and range overflow are handled,
  - whether unterminated strings are reported at the right location,
  - whether the current no-escape-string rule is explicit enough for MVP.

### 15-7. Dynamic array growth and VALUES list ownership
- Check `src/parser.c:269-357`.
- Review points:
  - whether a string value is freed if `realloc` fails,
  - whether `append_value` copies the union payload safely,
  - whether empty `VALUES ()` is rejected intentionally,
  - whether comma and closing parenthesis handling is strict enough.

### 15-8. Statement-level parse flow and cleanup on failure
- Check `src/parser.c:360-456`.
- Review points:
  - whether `INSERT` and `SELECT` paths are separated cleanly,
  - whether unsupported syntax fails early and explicitly,
  - whether the final semicolon and trailing-content checks correctly enforce one statement per file,
  - whether `statement_free(&stmt)` is called on every failure path after allocation may have happened,
  - whether ownership moves to `out` only on success.

### 15-9. CLI file read path and resource lifetime
- Check `src/main.c:15-63` and `src/main.c:98-133`.
- Review points:
  - whether file size discovery and full-buffer read are handled safely,
  - whether the SQL text buffer is always freed on parser failure,
  - whether `statement_free` is called on success,
  - whether Phase 1 intentionally stops after parse summary and does not leak into executor work.

### 15-10. Test coverage quality
- Check `tests/test_parser.c:15-161`.
- Review points:
  - whether the tests reflect the MVP grammar exactly,
  - whether both success and failure paths are covered,
  - whether unsupported syntax tests are strong enough,
  - whether any important edge case is still missing.

## 16. Explanation-to-Code Index

### "Why not `strtok`?"
- Primary code:
  - `src/parser.c:57-123`
  - `src/parser.c:396-456`
- Explain:
  - parser state is tracked with `pos`,
  - input is not mutated,
  - exact failure position is preserved,
  - grammar tokens are consumed under parser control.

### "How does the parser walk through the SQL string?"
- Primary code:
  - `src/parser.c:65-69`
  - `src/parser.c:112-123`
  - `src/parser.c:312-357`
  - `src/parser.c:396-456`
- Explain:
  - skip whitespace,
  - expect or match the next grammar element,
  - advance `pos`,
  - stop on the first invalid token and record the fault.

### "How is heap ownership managed?"
- Primary code:
  - `include/sql_processor.h:32-52`
  - `src/parser.c:72-85`
  - `src/parser.c:280-292`
  - `src/parser.c:396-456`
  - `src/statement.c:14-32`
- Explain:
  - copied identifiers and strings live on the heap,
  - values array grows with `realloc`,
  - successful parse transfers ownership to caller,
  - `statement_free` is the single cleanup gate.

### "Why use `out` and `err` pointers?"
- Primary code:
  - `include/sql_processor.h:48-66`
  - `src/parser.c:16-39`
  - `src/parser.c:396-456`
  - `src/main.c:119-127`
- Explain:
  - return value controls success vs failure,
  - `out` carries the AST only on success,
  - `err` carries structured failure detail,
  - caller logic stays linear and explicit.

### "Where is the one-statement-only rule enforced?"
- Primary code:
  - `src/parser.c:440-452`
- Explain:
  - parser requires `;`,
  - then skips trailing whitespace,
  - then rejects any remaining non-whitespace input.

### "Where are unsupported grammar decisions encoded?"
- Primary code:
  - `src/parser.c:324-327`
  - `src/parser.c:383-385`
  - `src/parser.c:435-437`
  - `src/parser.c:449-451`
  - `tests/test_parser.c:79-123`
- Explain:
  - empty `VALUES` is rejected,
  - only `SELECT *` is allowed,
  - only `INSERT` and `SELECT` statements are accepted,
  - multiple statements are rejected,
  - tests lock these choices in.

### "Where should Phase 1 review stop and Phase 2 begin?"
- Primary code:
  - `src/main.c:87-96`
  - `src/main.c:119-129`
  - `include/sql_processor.h:59-66`
- Explain:
  - `execute_statement` and `storage_append_row` exist only as contracts,
  - Phase 1 CLI stops after parse summary,
  - no CSV execution logic is present yet.
