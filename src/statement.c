#include "sql_types.h"

#include <stdlib.h>
#include <string.h>

/* NULL 안전하게 0으로 초기화해 호출자가 언제든 free 경로를 재사용할 수 있게 한다. */
void statement_init(Statement *stmt) {
    if (stmt == NULL) {
        return;
    }

    memset(stmt, 0, sizeof(*stmt));
}

/* Statement가 소유한 모든 힙 메모리를 한 곳에서 해제한다. */
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
