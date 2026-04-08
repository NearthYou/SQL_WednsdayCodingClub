#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "sql_error.h"
#include "sql_types.h"

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "Check failed: %s (line %d)\n", #condition, __LINE__);  \
            return 1;                                                               \
        }                                                                           \
    } while (0)

static int test_insert_simple(void) {
    const char *sql = "INSERT INTO users VALUES (1, 'alice', 20);";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_INSERT);
    CHECK(stmt.schema == NULL);
    CHECK(strcmp(stmt.table, "users") == 0);
    CHECK(stmt.value_count == 3);
    CHECK(stmt.values[0].type == SQL_VALUE_INT);
    CHECK(stmt.values[0].as.int_value == 1);
    CHECK(stmt.values[1].type == SQL_VALUE_STRING);
    CHECK(strcmp(stmt.values[1].as.string_value, "alice") == 0);
    CHECK(stmt.values[2].type == SQL_VALUE_INT);
    CHECK(stmt.values[2].as.int_value == 20);
    statement_free(&stmt);
    return 0;
}

static int test_select_schema_with_whitespace(void) {
    const char *sql = " \n SeLeCt   * \n FROM app . users \n ; ";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_SELECT);
    CHECK(strcmp(stmt.schema, "app") == 0);
    CHECK(strcmp(stmt.table, "users") == 0);
    CHECK(stmt.value_count == 0);
    statement_free(&stmt);
    return 0;
}

static int test_insert_negative_integer(void) {
    const char *sql = "INSERT INTO audit.logs VALUES (-12, 'ok');";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_INSERT);
    CHECK(strcmp(stmt.schema, "audit") == 0);
    CHECK(strcmp(stmt.table, "logs") == 0);
    CHECK(stmt.value_count == 2);
    CHECK(stmt.values[0].as.int_value == -12);
    statement_free(&stmt);
    return 0;
}

static int test_fail_missing_semicolon(void) {
    const char *sql = "SELECT * FROM users";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE);
    statement_free(&stmt);
    return 0;
}

static int test_fail_column_list(void) {
    const char *sql = "SELECT id FROM users;";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_UNSUPPORTED);
    statement_free(&stmt);
    return 0;
}

static int test_fail_unterminated_string(void) {
    const char *sql = "INSERT INTO users VALUES (1, 'alice);";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_LEX);
    statement_free(&stmt);
    return 0;
}

static int test_fail_multiple_statements(void) {
    const char *sql = "SELECT * FROM users; SELECT * FROM users;";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_UNSUPPORTED);
    statement_free(&stmt);
    return 0;
}

static int test_fail_where_clause(void) {
    const char *sql = "SELECT * FROM users WHERE id = 1;";
    Statement stmt;
    SqlError err;

    sql_error_clear(&err);
    statement_init(&stmt);
    CHECK(parse_sql(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE || err.code == SQL_ERR_UNSUPPORTED);
    statement_free(&stmt);
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        { "insert_simple", test_insert_simple },
        { "select_schema_with_whitespace", test_select_schema_with_whitespace },
        { "insert_negative_integer", test_insert_negative_integer },
        { "fail_missing_semicolon", test_fail_missing_semicolon },
        { "fail_column_list", test_fail_column_list },
        { "fail_unterminated_string", test_fail_unterminated_string },
        { "fail_multiple_statements", test_fail_multiple_statements },
        { "fail_where_clause", test_fail_where_clause }
    };
    size_t i;
    int failures;

    failures = 0;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (tests[i].fn() != 0) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            failures++;
        } else {
            printf("PASSED: %s\n", tests[i].name);
        }
    }

    if (failures != 0) {
        fprintf(stderr, "Parser tests failed: %d\n", failures);
        return EXIT_FAILURE;
    }

    printf("All parser tests passed.\n");
    return EXIT_SUCCESS;
}
