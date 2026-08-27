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
    /* INSERT column list 또는 SELECT projection 목록이다. */
    char **columns;
    size_t column_count;
    /* SELECT * 인지, 아니면 columns projection 인지 구분한다. */
    int select_all;
    /* SELECT WHERE column = literal 형태의 단일 조건이다. */
    int has_where;
    char *where_column;
    SqlValue where_value;
    SqlValue *values;
    size_t value_count;
} Statement;

/* SQL 파일 하나에서 파싱된 여러 문장을 순서대로 보관한다. */
typedef struct SqlScript {
    Statement *statements;
    size_t statement_count;
} SqlScript;

/* 성공 시 Statement 내부 문자열/배열 소유권은 호출자에게 넘어간다. */
void statement_init(Statement *stmt);
void statement_free(Statement *stmt);
void sql_script_init(SqlScript *script);
void sql_script_free(SqlScript *script);
const char *statement_type_name(StatementType type);

#endif
