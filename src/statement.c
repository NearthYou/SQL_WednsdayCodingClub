#include "sql_types.h"

#include <stdlib.h>
#include <string.h>

void statement_init(Statement *stmt) {
    if (stmt == NULL) {
        return;
    }

    memset(stmt, 0, sizeof(*stmt));
}

void statement_free(Statement *stmt) {
    size_t i;

    if (stmt == NULL) {
        return;
    }

    free(stmt->schema);
    free(stmt->table);

    for (i = 0; i < stmt->value_count; ++i) {
        if (stmt->values[i].type == SQL_VALUE_STRING) {
            free(stmt->values[i].as.string_value);
        }
    }

    free(stmt->values);
    statement_init(stmt);
}

const char *statement_type_name(StatementType type) {
    switch (type) {
        case STMT_INSERT:
            return "INSERT";
        case STMT_SELECT:
            return "SELECT";
        case STMT_NONE:
        default:
            return "NONE";
    }
}
