#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#define GET_PID() _getpid()
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#define GET_PID() getpid()
#endif

#include "execute.h"
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

#define CHECK_CONTAINS(haystack, needle)                                            \
    do {                                                                            \
        if (strstr((haystack), (needle)) == NULL) {                                 \
            fprintf(stderr,                                                          \
                    "Check failed: \"%s\" should contain \"%s\" (line %d)\n",       \
                    (haystack),                                                     \
                    (needle),                                                       \
                    __LINE__);                                                      \
            return 1;                                                               \
        }                                                                           \
    } while (0)

static int create_test_dir(char *buffer, size_t buffer_size) {
    int attempt;

    for (attempt = 0; attempt < 128; ++attempt) {
        if (snprintf(buffer,
                     buffer_size,
                     "test_tmp_%d_%d",
                     (int) GET_PID(),
                     attempt) >= (int) buffer_size) {
            return 1;
        }

        if (MAKE_DIR(buffer) == 0) {
            return 0;
        }

        if (errno != EEXIST) {
            return 1;
        }
    }

    return 1;
}

static void build_path(char *buffer, size_t buffer_size, const char *dir, const char *file_name) {
    snprintf(buffer, buffer_size, "%s/%s", dir, file_name);
}

static int write_text_file(const char *path, const char *contents) {
    FILE *file;

    file = fopen(path, "wb");
    if (file == NULL) {
        return 1;
    }

    if (contents != NULL && fwrite(contents, 1u, strlen(contents), file) != strlen(contents)) {
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

static char *read_text_file(const char *path) {
    FILE *file;
    long file_size;
    size_t read_size;
    char *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *) malloc((size_t) file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    read_size = fread(buffer, 1u, (size_t) file_size, file);
    if (read_size != (size_t) file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}

static void cleanup_test_path(const char *path) {
    if (path != NULL) {
        remove(path);
    }
}

static void cleanup_test_dir(const char *dir) {
    if (dir != NULL) {
        REMOVE_DIR(dir);
    }
}

static int parse_statement(const char *sql, Statement *stmt, SqlError *err) {
    sql_error_clear(err);
    statement_init(stmt);
    return parse_sql(sql, stmt, err);
}

static int test_insert_simple(void) {
    const char *sql = "INSERT INTO users VALUES (1, 'alice', 20);";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);
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

    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);
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

    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_INSERT);
    CHECK(strcmp(stmt.schema, "audit") == 0);
    CHECK(strcmp(stmt.table, "logs") == 0);
    CHECK(stmt.value_count == 2);
    CHECK(stmt.values[0].as.int_value == -12);
    statement_free(&stmt);
    return 0;
}

static int test_insert_with_utf8_bom(void) {
    const char *sql = "\xEF\xBB\xBFINSERT INTO users VALUES (11, '홍길동', 44);";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_INSERT);
    CHECK(strcmp(stmt.table, "users") == 0);
    CHECK(stmt.value_count == 3);
    CHECK(stmt.values[1].type == SQL_VALUE_STRING);
    CHECK(strcmp(stmt.values[1].as.string_value, "홍길동") == 0);
    statement_free(&stmt);
    return 0;
}

static int test_fail_missing_semicolon(void) {
    const char *sql = "SELECT * FROM users";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE);
    statement_free(&stmt);
    return 0;
}

static int test_fail_column_list(void) {
    const char *sql = "SELECT id FROM users;";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_UNSUPPORTED);
    statement_free(&stmt);
    return 0;
}

static int test_fail_unterminated_string(void) {
    const char *sql = "INSERT INTO users VALUES (1, 'alice);";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_LEX);
    statement_free(&stmt);
    return 0;
}

static int test_fail_trailing_comma_in_values(void) {
    const char *sql = "INSERT INTO users VALUES (1, 'alice',);";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE);
    statement_free(&stmt);
    return 0;
}

static int test_fail_multiple_statements(void) {
    const char *sql = "SELECT * FROM users; SELECT * FROM users;";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_UNSUPPORTED);
    statement_free(&stmt);
    return 0;
}

static int test_fail_where_clause(void) {
    const char *sql = "SELECT * FROM users WHERE id = 1;";
    Statement stmt;
    SqlError err;

    CHECK(parse_statement(sql, &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE || err.code == SQL_ERR_UNSUPPORTED);
    statement_free(&stmt);
    return 0;
}

static int test_execute_insert_appends_row(void) {
    const char *sql = "INSERT INTO users VALUES (2, 'bob', 30);";
    char dir[128];
    char csv_path[256];
    char output_path[256];
    char *csv_contents;
    char *output_contents;
    Statement stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(output_path, sizeof(output_path), dir, "insert_output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n") == 0);
    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

    output_file = fopen(output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&stmt, dir, output_file, &err) == SQL_SUCCESS);
    fclose(output_file);
    statement_free(&stmt);

    csv_contents = read_text_file(csv_path);
    output_contents = read_text_file(output_path);
    CHECK(csv_contents != NULL);
    CHECK(output_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n2,\"bob\",30\n") == 0);
    CHECK(strcmp(output_contents, "INSERT 1 INTO users\n") == 0);

    free(csv_contents);
    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_insert_fails_on_missing_csv(void) {
    const char *sql = "INSERT INTO users VALUES (2, 'bob', 30);";
    char dir[128];
    char output_path[256];
    Statement stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(output_path, sizeof(output_path), dir, "insert_output.txt");
    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

    output_file = fopen(output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&stmt, dir, output_file, &err) == SQL_FAILURE);
    fclose(output_file);
    CHECK(err.code == SQL_ERR_IO);
    CHECK_CONTAINS(err.message, "users.csv");

    statement_free(&stmt);
    cleanup_test_path(output_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_insert_fails_on_column_count_mismatch(void) {
    const char *sql = "INSERT INTO users VALUES (2, 'bob');";
    char dir[128];
    char csv_path[256];
    char output_path[256];
    Statement stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(output_path, sizeof(output_path), dir, "insert_output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n") == 0);
    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

    output_file = fopen(output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&stmt, dir, output_file, &err) == SQL_FAILURE);
    fclose(output_file);
    CHECK(err.code == SQL_ERR_ARGUMENT);
    CHECK_CONTAINS(err.message, "expects 3 values");

    statement_free(&stmt);
    cleanup_test_path(output_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_insert_escapes_quotes_and_schema_path(void) {
    const char *sql = "INSERT INTO app.users VALUES (1, 'a,b\"c', 2);";
    char dir[128];
    char csv_path[256];
    char output_path[256];
    char *csv_contents;
    Statement stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "app__users.csv");
    build_path(output_path, sizeof(output_path), dir, "insert_output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n") == 0);
    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

    output_file = fopen(output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&stmt, dir, output_file, &err) == SQL_SUCCESS);
    fclose(output_file);
    statement_free(&stmt);

    csv_contents = read_text_file(csv_path);
    CHECK(csv_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"a,b\"\"c\",2\n") == 0);

    free(csv_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_select_streams_csv(void) {
    const char *sql = "SELECT * FROM users;";
    char dir[128];
    char csv_path[256];
    char output_path[256];
    char *output_contents;
    Statement stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(output_path, sizeof(output_path), dir, "select_output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n2,\"bob\",30\n") == 0);
    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

    output_file = fopen(output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&stmt, dir, output_file, &err) == SQL_SUCCESS);
    fclose(output_file);
    statement_free(&stmt);

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents, "id,name,age\n1,\"alice\",20\n2,\"bob\",30\n") == 0);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_select_fails_on_empty_csv(void) {
    const char *sql = "SELECT * FROM users;";
    char dir[128];
    char csv_path[256];
    char output_path[256];
    Statement stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(output_path, sizeof(output_path), dir, "select_output.txt");
    CHECK(write_text_file(csv_path, "") == 0);
    CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

    output_file = fopen(output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&stmt, dir, output_file, &err) == SQL_FAILURE);
    fclose(output_file);
    CHECK(err.code == SQL_ERR_PARSE);
    CHECK_CONTAINS(err.message, "header row");

    statement_free(&stmt);
    cleanup_test_path(output_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_insert_then_select_round_trip(void) {
    const char *insert_sql = "INSERT INTO users VALUES (3, 'carol', 28);";
    const char *select_sql = "SELECT * FROM users;";
    char dir[128];
    char csv_path[256];
    char insert_output_path[256];
    char select_output_path[256];
    char *select_output;
    Statement insert_stmt;
    Statement select_stmt;
    SqlError err;
    FILE *output_file;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(insert_output_path, sizeof(insert_output_path), dir, "insert_output.txt");
    build_path(select_output_path, sizeof(select_output_path), dir, "select_output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n") == 0);

    CHECK(parse_statement(insert_sql, &insert_stmt, &err) == SQL_SUCCESS);
    output_file = fopen(insert_output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&insert_stmt, dir, output_file, &err) == SQL_SUCCESS);
    fclose(output_file);
    statement_free(&insert_stmt);

    CHECK(parse_statement(select_sql, &select_stmt, &err) == SQL_SUCCESS);
    output_file = fopen(select_output_path, "wb");
    CHECK(output_file != NULL);
    CHECK(execute_statement(&select_stmt, dir, output_file, &err) == SQL_SUCCESS);
    fclose(output_file);
    statement_free(&select_stmt);

    select_output = read_text_file(select_output_path);
    CHECK(select_output != NULL);
    CHECK(strcmp(select_output, "id,name,age\n1,\"alice\",20\n3,\"carol\",28\n") == 0);

    free(select_output);
    cleanup_test_path(select_output_path);
    cleanup_test_path(insert_output_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
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
        { "insert_with_utf8_bom", test_insert_with_utf8_bom },
        { "fail_missing_semicolon", test_fail_missing_semicolon },
        { "fail_column_list", test_fail_column_list },
        { "fail_unterminated_string", test_fail_unterminated_string },
        { "fail_trailing_comma_in_values", test_fail_trailing_comma_in_values },
        { "fail_multiple_statements", test_fail_multiple_statements },
        { "fail_where_clause", test_fail_where_clause },
        { "execute_insert_appends_row", test_execute_insert_appends_row },
        { "execute_insert_fails_on_missing_csv", test_execute_insert_fails_on_missing_csv },
        { "execute_insert_fails_on_column_count_mismatch", test_execute_insert_fails_on_column_count_mismatch },
        { "execute_insert_escapes_quotes_and_schema_path", test_execute_insert_escapes_quotes_and_schema_path },
        { "execute_select_streams_csv", test_execute_select_streams_csv },
        { "execute_select_fails_on_empty_csv", test_execute_select_fails_on_empty_csv },
        { "execute_insert_then_select_round_trip", test_execute_insert_then_select_round_trip }
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
        fprintf(stderr, "Tests failed: %d\n", failures);
        return EXIT_FAILURE;
    }

    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
