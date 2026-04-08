#include "storage.h"

int storage_append_row(const char *data_dir,
                       const char *schema,
                       const char *table,
                       const SqlValue *values,
                       size_t value_count,
                       SqlError *err) {
    (void) data_dir;
    (void) schema;
    (void) table;
    (void) values;
    (void) value_count;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_set(err,
                  SQL_ERR_NOT_IMPLEMENTED,
                  0,
                  "storage_append_row is reserved for Phase 2");
    return SQL_FAILURE;
}
