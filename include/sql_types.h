#ifndef SQL_TYPES_H
#define SQL_TYPES_H

#include <stddef.h>

typedef enum StatementType {
    STMT_NONE = 0,
    STMT_INSERT,
    STMT_SELECT
} StatementType;

typedef enum SqlValueType {
    SQL_VALUE_INT = 0,
    SQL_VALUE_STRING
} SqlValueType;

/* SQL 리터럴 한 개를 표현한다. */
typedef struct SqlValue {
    SqlValueType type;
    union {
        long long int_value;
        char *string_value;
    } as;
} SqlValue;

/* parser가 만들어 내는 최소 AST 구조다. */
typedef struct Statement {
    StatementType type;
    char *schema;
    char *table;
    SqlValue *values;
    size_t value_count;
} Statement;

/* 성공 시 Statement 내부 문자열/배열 소유권은 호출자에게 넘어간다. */
void statement_init(Statement *stmt);
void statement_free(Statement *stmt);
const char *statement_type_name(StatementType type);

#endif
