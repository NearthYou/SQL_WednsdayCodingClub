#ifndef EXECUTE_H
#define EXECUTE_H

#include <stdio.h>

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

/* Phase 2에서 AST를 실제 동작으로 연결하는 실행기 진입점이다. */
int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err);

#endif
