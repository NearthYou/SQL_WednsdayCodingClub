#ifndef EXECUTE_H
#define EXECUTE_H

#include <stdio.h>

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

/* 단일 Statement 호환 실행기다. */
int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err);
/* 여러 문장을 staging/rollback 규칙으로 실행하는 Phase 3 진입점이다. */
int execute_script(const SqlScript *script, const char *data_dir, FILE *out, SqlError *err);

#endif
