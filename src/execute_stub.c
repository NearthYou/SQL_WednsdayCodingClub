#include "execute.h"

/* Phase 2 전까지는 링크 구조만 유지하고 실제 실행은 막는다. */
int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err) {
    (void) stmt;
    (void) data_dir;
    (void) out;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_set(err,
                  SQL_ERR_NOT_IMPLEMENTED,
                  0,
                  "execute_statement is reserved for Phase 2");
    return SQL_FAILURE;
}
