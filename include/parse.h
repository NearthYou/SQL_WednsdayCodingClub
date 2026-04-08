#ifndef PARSE_H
#define PARSE_H

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

int parse_sql(const char *sql, Statement *out, SqlError *err);

#endif
