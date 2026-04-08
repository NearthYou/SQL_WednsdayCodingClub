#include "execute.h"
#include "storage.h"

#include <stdio.h>

int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err) {
    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);

    if (stmt == NULL || data_dir == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "stmt, data_dir, and out are required");
        return SQL_FAILURE;
    }

    if (stmt->table == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "Statement target table is required");
        return SQL_FAILURE;
    }

    if (stmt->type == STMT_INSERT) {
        if (storage_append_row(data_dir,
                               stmt->schema,
                               stmt->table,
                               stmt->values,
                               stmt->value_count,
                               err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (fprintf(out, "INSERT 1 INTO %s%s%s\n",
                    (stmt->schema != NULL) ? stmt->schema : "",
                    (stmt->schema != NULL) ? "." : "",
                    stmt->table) < 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write INSERT result");
            return SQL_FAILURE;
        }

        if (fflush(out) != 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to flush INSERT result");
            return SQL_FAILURE;
        }

        return SQL_SUCCESS;
    }

    if (stmt->type == STMT_SELECT) {
        return storage_select_all(data_dir, stmt->schema, stmt->table, out, err);
    }

    sql_error_set(err, SQL_ERR_UNSUPPORTED, 0, "Unsupported statement type for execution");
    return SQL_FAILURE;
}
