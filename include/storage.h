#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdio.h>

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

/* schema 유무를 보고 positional INSERT 또는 column-name INSERT를 CSV로 반영한다. */
int storage_append_row(const char *data_dir,
                       const char *schema,
                       const char *table,
                       const char *const *column_names,
                       size_t column_count,
                       const SqlValue *values,
                       size_t value_count,
                       SqlError *err);

/* SELECT * 또는 SELECT col1, col2 projection을 CSV에서 읽어 출력한다. */
int storage_select_projection(const char *data_dir,
                              const char *schema,
                              const char *table,
                              const char *const *column_names,
                              size_t column_count,
                              int select_all,
                              FILE *out,
                              SqlError *err);

/* 기존 SELECT * 호출 경로를 위한 호환 wrapper다. */
int storage_select_all(const char *data_dir,
                       const char *schema,
                       const char *table,
                       FILE *out,
                       SqlError *err);

#endif
