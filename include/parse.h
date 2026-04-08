#ifndef PARSE_H
#define PARSE_H

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

/* 단일 Statement 호환 API다. 내부적으로 script parser를 호출하고 문장 하나만 허용한다. */
int parse_sql(const char *sql, Statement *out, SqlError *err);
/* 여러 SQL 문장을 순서대로 파싱해 Script AST로 돌려준다. */
int parse_sql_script(const char *sql, SqlScript *out, SqlError *err);

#endif
