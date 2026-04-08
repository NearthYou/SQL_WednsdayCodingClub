#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdio.h>

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

/* Phase 2에서 VALUES를 CSV 행으로 저장하는 저장소 경계다. */
int storage_append_row(const char *data_dir,
                       const char *schema,
                       const char *table,
                       const SqlValue *values,
                       size_t value_count,
                       SqlError *err);

/* Phase 2에서 CSV 전체를 헤더 포함 그대로 출력하는 저장소 경계다. */
int storage_select_all(const char *data_dir,
                       const char *schema,
                       const char *table,
                       FILE *out,
                       SqlError *err);

#endif
