#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

#include "sql_common.h"
#include "sql_error.h"
#include "sql_types.h"

int storage_append_row(const char *data_dir,
                       const char *schema,
                       const char *table,
                       const SqlValue *values,
                       size_t value_count,
                       SqlError *err);

#endif
