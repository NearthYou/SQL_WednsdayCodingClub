#ifndef EXECUTE_H
#define EXECUTE_H

#include <stdio.h>

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err);

#endif
