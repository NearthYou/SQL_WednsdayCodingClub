#ifndef SQL_ERROR_H
#define SQL_ERROR_H

#include <stddef.h>

typedef enum SqlErrorCode {
    SQL_ERR_NONE = 0,
    SQL_ERR_ARGUMENT,
    SQL_ERR_IO,
    SQL_ERR_LEX,
    SQL_ERR_PARSE,
    SQL_ERR_UNSUPPORTED,
    SQL_ERR_MEMORY,
    SQL_ERR_NOT_IMPLEMENTED
} SqlErrorCode;

/* 실패 원인을 코드, 위치, 메시지로 함께 전달한다. */
typedef struct SqlError {
    SqlErrorCode code;
    size_t position;
    char message[256];
} SqlError;

/* 에러 구조체를 재사용하기 쉽게 초기화한다. */
void sql_error_clear(SqlError *err);
void sql_error_set(SqlError *err, SqlErrorCode code, size_t position, const char *fmt, ...);
const char *sql_error_code_name(SqlErrorCode code);

#endif
