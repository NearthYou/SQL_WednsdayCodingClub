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

typedef struct SqlValue {
    SqlValueType type;
    union {
        long long int_value;
        char *string_value;
    } as;
} SqlValue;

typedef struct Statement {
    StatementType type;
    char *schema;
    char *table;
    SqlValue *values;
    size_t value_count;
} Statement;

void statement_init(Statement *stmt);
void statement_free(Statement *stmt);
const char *statement_type_name(StatementType type);

#endif
