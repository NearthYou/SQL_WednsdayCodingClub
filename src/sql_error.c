#include "sql_error.h"

#include <stdarg.h>
#include <stdio.h>

void sql_error_clear(SqlError *err) {
    if (err == NULL) {
        return;
    }

    err->code = SQL_ERR_NONE;
    err->position = 0;
    err->message[0] = '\0';
}

void sql_error_set(SqlError *err, SqlErrorCode code, size_t position, const char *fmt, ...) {
    va_list args;

    if (err == NULL) {
        return;
    }

    err->code = code;
    err->position = position;

    if (fmt == NULL) {
        err->message[0] = '\0';
        return;
    }

    va_start(args, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);
    err->message[sizeof(err->message) - 1] = '\0';
}

const char *sql_error_code_name(SqlErrorCode code) {
    switch (code) {
        case SQL_ERR_NONE:
            return "NONE";
        case SQL_ERR_ARGUMENT:
            return "ARGUMENT";
        case SQL_ERR_IO:
            return "IO";
        case SQL_ERR_LEX:
            return "LEX";
        case SQL_ERR_PARSE:
            return "PARSE";
        case SQL_ERR_UNSUPPORTED:
            return "UNSUPPORTED";
        case SQL_ERR_MEMORY:
            return "MEMORY";
        case SQL_ERR_NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED";
        default:
            return "UNKNOWN";
    }
}
