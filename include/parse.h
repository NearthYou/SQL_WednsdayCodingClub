#ifndef PARSE_H
#define PARSE_H

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

/* 성공하면 out이 완성된 AST를 소유하고, 실패하면 err에 원인을 기록한다. */
int parse_sql(const char *sql, Statement *out, SqlError *err);

#endif
