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
        if (snprintf(buffer, buffer_size, "test_tmp_%d_%d", (int) GET_PID(), attempt) >= (int) buffer_size) {
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
    size_t length;

    file = fopen(path, "wb");
    if (file == NULL) {
        return 1;
    }

    length = (contents != NULL) ? strlen(contents) : 0;
    if (length > 0 && fwrite(contents, 1u, length, file) != length) {
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

static char *read_text_file(const char *path) {
    FILE *file;
    long size;
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

    size = ftell(file);
    if (size < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *) malloc((size_t) size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    read_size = fread(buffer, 1u, (size_t) size, file);
    fclose(file);
    if (read_size != (size_t) size) {
        free(buffer);
        return NULL;
    }

    buffer[read_size] = '\0';
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

static int parse_script(const char *sql, SqlScript *script, SqlError *err) {
    sql_error_clear(err);
    sql_script_init(script);
    return parse_sql_script(sql, script, err);
}

static int execute_sql_script_to_file(const char *sql, const char *data_dir, const char *output_path, SqlError *err) {
    SqlScript script;
    FILE *output;
    int result;

    sql_script_init(&script);
    result = parse_script(sql, &script, err);
    if (result != SQL_SUCCESS) {
        return result;
    }

    output = fopen(output_path, "wb");
    if (output == NULL) {
        sql_script_free(&script);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open output file '%s'", output_path);
        return SQL_FAILURE;
    }

    result = execute_script(&script, data_dir, output, err);
    fclose(output);
    sql_script_free(&script);
    return result;
}

static int test_parse_insert_column_list(void) {
    Statement stmt;
    SqlError err;

    CHECK(parse_statement("INSERT INTO users (name, id, age) VALUES ('alice', 1, 20);", &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_INSERT);
    CHECK(stmt.column_count == 3);
    CHECK(strcmp(stmt.columns[0], "name") == 0);
    CHECK(strcmp(stmt.columns[1], "id") == 0);
    CHECK(strcmp(stmt.columns[2], "age") == 0);
    CHECK(stmt.value_count == 3);
    CHECK(stmt.values[0].type == SQL_VALUE_STRING);
    CHECK(strcmp(stmt.values[0].as.string_value, "alice") == 0);
    CHECK(stmt.values[1].type == SQL_VALUE_INT);
    CHECK(stmt.values[1].as.int_value == 1);
    statement_free(&stmt);
    return 0;
}

static int test_parse_select_projection(void) {
    Statement stmt;
    SqlError err;

    CHECK(parse_statement("SELECT name, age FROM app.users;", &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.type == STMT_SELECT);
    CHECK(stmt.select_all == 0);
    CHECK(stmt.column_count == 2);
    CHECK(strcmp(stmt.schema, "app") == 0);
    CHECK(strcmp(stmt.table, "users") == 0);
    CHECK(strcmp(stmt.columns[0], "name") == 0);
    CHECK(strcmp(stmt.columns[1], "age") == 0);
    statement_free(&stmt);
    return 0;
}

static int test_parse_string_escape_and_bom(void) {
    Statement stmt;
    SqlError err;

    CHECK(parse_statement("\xEF\xBB\xBFINSERT INTO users VALUES (1, 'it''s ok', 20);", &stmt, &err) == SQL_SUCCESS);
    CHECK(stmt.values[1].type == SQL_VALUE_STRING);
    CHECK(strcmp(stmt.values[1].as.string_value, "it's ok") == 0);
    statement_free(&stmt);
    return 0;
}

static int test_parse_multiple_statements(void) {
    SqlScript script;
    SqlError err;

    CHECK(parse_script("INSERT INTO users VALUES (1, 'alice', 20); SELECT name FROM users;", &script, &err) == SQL_SUCCESS);
    CHECK(script.statement_count == 2);
    CHECK(script.statements[0].type == STMT_INSERT);
    CHECK(script.statements[1].type == STMT_SELECT);
    CHECK(script.statements[1].column_count == 1);
    CHECK(strcmp(script.statements[1].columns[0], "name") == 0);
    sql_script_free(&script);
    return 0;
}

static int test_parse_fail_duplicate_identifier(void) {
    Statement stmt;
    SqlError err;

    CHECK(parse_statement("SELECT id, id FROM users;", &stmt, &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE);
    CHECK_CONTAINS(err.message, "Duplicate identifier");
    statement_free(&stmt);
    return 0;
}

static int test_execute_schema_insert_reorders_columns(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *csv_contents;
    char *output_contents;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("INSERT INTO users (name, id, age) VALUES ('alice', 1, 20);",
                                     dir,
                                     output_path,
                                     &err) == SQL_SUCCESS);

    csv_contents = read_text_file(csv_path);
    output_contents = read_text_file(output_path);
    CHECK(csv_contents != NULL);
    CHECK(output_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(strcmp(output_contents, "INSERT 1 INTO users\n") == 0);

    free(csv_contents);
    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_auto_create_csv_from_schema(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *csv_contents;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("INSERT INTO users (id, name, age) VALUES (7, 'neo', 99);",
                                     dir,
                                     output_path,
                                     &err) == SQL_SUCCESS);

    csv_contents = read_text_file(csv_path);
    CHECK(csv_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n7,\"neo\",99\n") == 0);

    free(csv_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_type_validation_failure(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *csv_contents;
    char *output_contents;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("INSERT INTO users VALUES ('bad', 'bob', 30);",
                                     dir,
                                     output_path,
                                     &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_ARGUMENT);
    CHECK_CONTAINS(err.message, "expects INT");

    csv_contents = read_text_file(csv_path);
    output_contents = read_text_file(output_path);
    CHECK(csv_contents != NULL);
    CHECK(output_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(strcmp(output_contents, "") == 0);

    free(csv_contents);
    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_select_projection(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *output_contents;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n2,\"bob\",30\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("SELECT name, age FROM users;",
                                     dir,
                                     output_path,
                                     &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents, "name,age\n\"alice\",20\n\"bob\",30\n") == 0);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_script_insert_then_select_sees_staged_row(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *output_contents;
    char *csv_contents;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("INSERT INTO users (id, name, age) VALUES (2, 'bob', 30); SELECT name, age FROM users;",
                                     dir,
                                     output_path,
                                     &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    csv_contents = read_text_file(csv_path);
    CHECK(output_contents != NULL);
    CHECK(csv_contents != NULL);
    CHECK(strcmp(output_contents, "INSERT 1 INTO users\nname,age\n\"alice\",20\n\"bob\",30\n") == 0);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n2,\"bob\",30\n") == 0);

    free(output_contents);
    free(csv_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_script_rollback_on_failure(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *csv_contents;
    char *output_contents;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("INSERT INTO users (id, name, age) VALUES (2, 'bob', 30); INSERT INTO users VALUES ('bad', 'oops', 10); SELECT * FROM users;",
                                     dir,
                                     output_path,
                                     &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_ARGUMENT);

    csv_contents = read_text_file(csv_path);
    output_contents = read_text_file(output_path);
    CHECK(csv_contents != NULL);
    CHECK(output_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(strcmp(output_contents, "") == 0);

    free(csv_contents);
    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        { "parse_insert_column_list", test_parse_insert_column_list },
        { "parse_select_projection", test_parse_select_projection },
        { "parse_string_escape_and_bom", test_parse_string_escape_and_bom },
        { "parse_multiple_statements", test_parse_multiple_statements },
        { "parse_fail_duplicate_identifier", test_parse_fail_duplicate_identifier },
        { "execute_schema_insert_reorders_columns", test_execute_schema_insert_reorders_columns },
        { "execute_auto_create_csv_from_schema", test_execute_auto_create_csv_from_schema },
        { "execute_type_validation_failure", test_execute_type_validation_failure },
        { "execute_select_projection", test_execute_select_projection },
        { "execute_script_insert_then_select_sees_staged_row", test_execute_script_insert_then_select_sees_staged_row },
        { "execute_script_rollback_on_failure", test_execute_script_rollback_on_failure }
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
