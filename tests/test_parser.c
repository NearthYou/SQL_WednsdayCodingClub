#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

static int path_exists(const char *path) {
    FILE *file;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    return 1;
}

static void cleanup_test_path(const char *path) {
    if (path != NULL) {
        remove(path);
    }
}

static void cleanup_test_dir(const char *dir) {
    if (dir != NULL) {
        if (REMOVE_DIR(dir) != 0) {
            /* Test cleanup is best-effort. */
        }
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

static int execute_sql_script_formatted_to_file(const char *sql,
                                                const char *data_dir,
                                                ExecuteOutputMode output_mode,
                                                const char *output_path,
                                                SqlError *err) {
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

    result = execute_script_formatted(&script, data_dir, output_mode, output, err);
    fclose(output);
    sql_script_free(&script);
    return result;
}

static int execute_sql_script_to_file(const char *sql, const char *data_dir, const char *output_path, SqlError *err) {
    return execute_sql_script_formatted_to_file(sql, data_dir, EXECUTE_OUTPUT_RAW, output_path, err);
}

static int execute_sql_script_formatted_to_stream(const char *sql,
                                                  const char *data_dir,
                                                  ExecuteOutputMode output_mode,
                                                  FILE *output,
                                                  SqlError *err) {
    SqlScript script;
    int result;

    sql_script_init(&script);
    result = parse_script(sql, &script, err);
    if (result != SQL_SUCCESS) {
        return result;
    }

    result = execute_script_formatted(&script, data_dir, output_mode, output, err);
    sql_script_free(&script);
    return result;
}

static int execute_sql_script_to_stream(const char *sql, const char *data_dir, FILE *output, SqlError *err) {
    return execute_sql_script_formatted_to_stream(sql, data_dir, EXECUTE_OUTPUT_RAW, output, err);
}

static void build_table_file_name(char *buffer,
                                  size_t buffer_size,
                                  const char *schema,
                                  const char *table,
                                  const char *suffix) {
    if (schema != NULL) {
        snprintf(buffer, buffer_size, "%s__%s%s", schema, table, suffix);
    } else {
        snprintf(buffer, buffer_size, "%s%s", table, suffix);
    }
}

static int append_format(char *buffer, size_t buffer_size, const char *fmt, ...) {
    va_list args;
    size_t length;
    int written;

    length = strlen(buffer);
    if (length >= buffer_size) {
        return 1;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer + length, buffer_size - length, fmt, args);
    va_end(args);
    if (written < 0 || (size_t) written >= buffer_size - length) {
        return 1;
    }

    return 0;
}

static void build_case_name(char *buffer, size_t buffer_size, size_t index) {
    switch (index % 10) {
        case 0:
            snprintf(buffer, buffer_size, "user_%u", (unsigned) index);
            break;
        case 1:
            snprintf(buffer, buffer_size, "user %u", (unsigned) index);
            break;
        case 2:
            snprintf(buffer, buffer_size, "o'malley_%u", (unsigned) index);
            break;
        case 3:
            snprintf(buffer, buffer_size, "comma,%u", (unsigned) index);
            break;
        case 4:
            snprintf(buffer, buffer_size, "\"quote\"_%u", (unsigned) index);
            break;
        case 5:
            snprintf(buffer, buffer_size, "semi;%u", (unsigned) index);
            break;
        case 6:
            snprintf(buffer, buffer_size, "slash/%u", (unsigned) index);
            break;
        case 7:
            snprintf(buffer, buffer_size, " trail_%u ", (unsigned) index);
            break;
        case 8:
            buffer[0] = '\0';
            break;
        case 9:
        default:
            snprintf(buffer, buffer_size, "mix_'\"_,;%u", (unsigned) index);
            break;
    }
}

static int append_sql_string_literal(char *buffer, size_t buffer_size, const char *text) {
    size_t i;

    if (append_format(buffer, buffer_size, "'") != 0) {
        return 1;
    }

    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\'') {
            if (append_format(buffer, buffer_size, "''") != 0) {
                return 1;
            }
        } else if (append_format(buffer, buffer_size, "%c", text[i]) != 0) {
            return 1;
        }
    }

    return append_format(buffer, buffer_size, "'");
}

static int append_insert_statement(char *buffer,
                                   size_t buffer_size,
                                   const char *schema,
                                   const char *table,
                                   int id,
                                   const char *name,
                                   int age,
                                   int bad_id_type) {
    if (append_format(buffer,
                      buffer_size,
                      "INSERT INTO %s%s%s (id, name, age) VALUES (",
                      (schema != NULL) ? schema : "",
                      (schema != NULL) ? "." : "",
                      table) != 0) {
        return 1;
    }

    if (bad_id_type) {
        if (append_sql_string_literal(buffer, buffer_size, "bad_id") != 0) {
            return 1;
        }
    } else if (append_format(buffer, buffer_size, "%d", id) != 0) {
        return 1;
    }

    if (append_format(buffer, buffer_size, ", ") != 0 ||
        append_sql_string_literal(buffer, buffer_size, name) != 0 ||
        append_format(buffer, buffer_size, ", %d);", age) != 0) {
        return 1;
    }

    return 0;
}

static int append_select_projection_statement(char *buffer,
                                              size_t buffer_size,
                                              const char *schema,
                                              const char *table,
                                              int select_all) {
    return append_format(buffer,
                         buffer_size,
                         "SELECT %s FROM %s%s%s;",
                         select_all ? "*" : "name, age",
                         (schema != NULL) ? schema : "",
                         (schema != NULL) ? "." : "",
                         table);
}

static int write_standard_schema_file(const char *dir, const char *schema, const char *table, char *schema_path, size_t schema_path_size) {
    char schema_file[128];

    build_table_file_name(schema_file, sizeof(schema_file), schema, table, ".schema.csv");
    build_path(schema_path, schema_path_size, dir, schema_file);
    return write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n");
}

static int write_standard_csv_file(const char *dir,
                                   const char *schema,
                                   const char *table,
                                   char *csv_path,
                                   size_t csv_path_size,
                                   const char *contents) {
    char csv_file[128];

    build_table_file_name(csv_file, sizeof(csv_file), schema, table, ".csv");
    build_path(csv_path, csv_path_size, dir, csv_file);
    return write_text_file(csv_path, contents);
}

static void build_invalid_csv_contents(char *buffer, size_t buffer_size, size_t index) {
    unsigned value;

    value = (unsigned) index;
    switch (index % 10) {
        case 0:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"alice_%u\"\n", value + 1u, value);
            break;
        case 1:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"alice_%u\",20,%u\n", value + 1u, value, value + 99u);
            break;
        case 2:
            snprintf(buffer, buffer_size, "id,name,age\n\n");
            break;
        case 3:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"alice_%u\"x,20\n", value + 1u, value);
            break;
        case 4:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"alice_%u,20\n", value + 1u, value);
            break;
        case 5:
            snprintf(buffer, buffer_size, "id,name,age\r\n%u,\"alice_%u\"\r\n", value + 1u, value);
            break;
        case 6:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"a\"\"b\"\"c\"\"d\"oops,20\n", value + 1u);
            break;
        case 7:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"alice_%u\",20\n%u,\"bob_%u\"\n", value + 1u, value, value + 2u, value);
            break;
        case 8:
            snprintf(buffer, buffer_size, "id,name,age\n%u,plain,%u,%u\n", value + 1u, value + 20u, value + 30u);
            break;
        case 9:
        default:
            snprintf(buffer, buffer_size, "id,name,age\n%u,\"alice_%u\",20\r\n\r\n", value + 1u, value);
            break;
    }
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

static int test_execute_select_all_pretty_ascii(void) {
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

    CHECK(execute_sql_script_formatted_to_file("SELECT * FROM users;",
                                               dir,
                                               EXECUTE_OUTPUT_PRETTY_ASCII,
                                               output_path,
                                               &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents,
                 "+----+-------+-----+\n"
                 "| id | name  | age |\n"
                 "+----+-------+-----+\n"
                 "|  1 | alice |  20 |\n"
                 "|  2 | bob   |  30 |\n"
                 "+----+-------+-----+\n") == 0);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_select_projection_pretty_ascii(void) {
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

    CHECK(execute_sql_script_formatted_to_file("SELECT name, age FROM users;",
                                               dir,
                                               EXECUTE_OUTPUT_PRETTY_ASCII,
                                               output_path,
                                               &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents,
                 "+-------+-----+\n"
                 "| name  | age |\n"
                 "+-------+-----+\n"
                 "| alice |  20 |\n"
                 "| bob   |  30 |\n"
                 "+-------+-----+\n") == 0);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_select_pretty_empty_result(void) {
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
    CHECK(write_text_file(csv_path, "id,name,age\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_formatted_to_file("SELECT * FROM users;",
                                               dir,
                                               EXECUTE_OUTPUT_PRETTY_ASCII,
                                               output_path,
                                               &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents,
                 "+----+------+-----+\n"
                 "| id | name | age |\n"
                 "+----+------+-----+\n"
                 "+----+------+-----+\n") == 0);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_script_insert_then_select_pretty_ascii(void) {
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

    CHECK(execute_sql_script_formatted_to_file("INSERT INTO users (id, name, age) VALUES (2, 'bob', 30); SELECT name, age FROM users;",
                                               dir,
                                               EXECUTE_OUTPUT_PRETTY_ASCII,
                                               output_path,
                                               &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    csv_contents = read_text_file(csv_path);
    CHECK(output_contents != NULL);
    CHECK(csv_contents != NULL);
    CHECK(strcmp(output_contents,
                 "INSERT 1 INTO users\n"
                 "+-------+-----+\n"
                 "| name  | age |\n"
                 "+-------+-----+\n"
                 "| alice |  20 |\n"
                 "| bob   |  30 |\n"
                 "+-------+-----+\n") == 0);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n2,\"bob\",30\n") == 0);

    free(output_contents);
    free(csv_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_select_pretty_strings_render_human_values(void) {
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
    CHECK(write_text_file(csv_path,
                          "id,name,age\n"
                          "1,\"space name\",20\n"
                          "2,\"comma,2\",30\n"
                          "3,\"\"\"quote\"\"\",40\n"
                          "4,\"o'malley\",50\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_formatted_to_file("SELECT name, age FROM users;",
                                               dir,
                                               EXECUTE_OUTPUT_PRETTY_ASCII,
                                               output_path,
                                               &err) == SQL_SUCCESS);

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents,
                 "+------------+-----+\n"
                 "| name       | age |\n"
                 "+------------+-----+\n"
                 "| space name |  20 |\n"
                 "| comma,2    |  30 |\n"
                 "| \"quote\"    |  40 |\n"
                 "| o'malley   |  50 |\n"
                 "+------------+-----+\n") == 0);
    CHECK(strstr(output_contents, "\"\"quote\"\"") == NULL);
    CHECK(strstr(output_contents, "\"comma,2\"") == NULL);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_pretty_output_failure_rolls_back_committed_data(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *csv_contents;
    char *output_contents;
    FILE *output;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);
    CHECK(write_text_file(output_path, "seed") == 0);

    output = fopen(output_path, "rb");
    CHECK(output != NULL);
    CHECK(execute_sql_script_formatted_to_stream("INSERT INTO users (id, name, age) VALUES (2, 'bob', 30); SELECT name, age FROM users;",
                                                 dir,
                                                 EXECUTE_OUTPUT_PRETTY_ASCII,
                                                 output,
                                                 &err) == SQL_FAILURE);
    fclose(output);
    CHECK(err.code == SQL_ERR_IO);

    csv_contents = read_text_file(csv_path);
    output_contents = read_text_file(output_path);
    CHECK(csv_contents != NULL);
    CHECK(output_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(strcmp(output_contents, "seed") == 0);

    free(csv_contents);
    free(output_contents);
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

static int test_execute_select_all_rejects_invalid_row_shape(void) {
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
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n2,\"bob\"\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);

    CHECK(execute_sql_script_to_file("SELECT * FROM users;",
                                     dir,
                                     output_path,
                                     &err) == SQL_FAILURE);
    CHECK(err.code == SQL_ERR_PARSE);
    CHECK_CONTAINS(err.message, "header has 3");

    output_contents = read_text_file(output_path);
    CHECK(output_contents != NULL);
    CHECK(strcmp(output_contents, "") == 0);

    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_output_failure_rolls_back_committed_data(void) {
    char dir[128];
    char csv_path[256];
    char schema_path[256];
    char output_path[256];
    char *csv_contents;
    char *output_contents;
    FILE *output;
    SqlError err;

    CHECK(create_test_dir(dir, sizeof(dir)) == 0);
    build_path(csv_path, sizeof(csv_path), dir, "users.csv");
    build_path(schema_path, sizeof(schema_path), dir, "users.schema.csv");
    build_path(output_path, sizeof(output_path), dir, "output.txt");
    CHECK(write_text_file(csv_path, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(write_text_file(schema_path, "name,type\nid,INT\nname,STRING\nage,INT\n") == 0);
    CHECK(write_text_file(output_path, "seed") == 0);

    output = fopen(output_path, "rb");
    CHECK(output != NULL);
    CHECK(execute_sql_script_to_stream("INSERT INTO users (id, name, age) VALUES (2, 'bob', 30);",
                                       dir,
                                       output,
                                       &err) == SQL_FAILURE);
    fclose(output);
    CHECK(err.code == SQL_ERR_IO);

    csv_contents = read_text_file(csv_path);
    output_contents = read_text_file(output_path);
    CHECK(csv_contents != NULL);
    CHECK(output_contents != NULL);
    CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
    CHECK(strcmp(output_contents, "seed") == 0);

    free(csv_contents);
    free(output_contents);
    cleanup_test_path(output_path);
    cleanup_test_path(schema_path);
    cleanup_test_path(csv_path);
    cleanup_test_dir(dir);
    return 0;
}

static int test_execute_script_rollback_on_failure_50_cases(void) {
    size_t i;

    for (i = 0; i < 50; ++i) {
        char dir[128];
        char csv_path[256];
        char schema_path[256];
        char output_path[256];
        char name_one[128];
        char name_two[128];
        char sql[4096];
        char *csv_contents;
        char *output_contents;
        const char *schema;
        int has_existing_csv;
        SqlError err;

        schema = (i % 2 == 0) ? NULL : "app";
        has_existing_csv = ((i / 2) % 2 == 0);
        build_case_name(name_one, sizeof(name_one), i);
        build_case_name(name_two, sizeof(name_two), i + 100);

        CHECK(create_test_dir(dir, sizeof(dir)) == 0);
        CHECK(write_standard_schema_file(dir, schema, "users", schema_path, sizeof(schema_path)) == 0);
        if (has_existing_csv) {
            CHECK(write_standard_csv_file(dir,
                                          schema,
                                          "users",
                                          csv_path,
                                          sizeof(csv_path),
                                          "id,name,age\n1,\"alice\",20\n") == 0);
        } else {
            char csv_file[128];

            build_table_file_name(csv_file, sizeof(csv_file), schema, "users", ".csv");
            build_path(csv_path, sizeof(csv_path), dir, csv_file);
        }
        build_path(output_path, sizeof(output_path), dir, "output.txt");

        sql[0] = '\0';
        CHECK(append_insert_statement(sql, sizeof(sql), schema, "users", 100 + (int) i, name_one, 20 + (int) i, 0) == 0);
        if (i % 3 == 0) {
            CHECK(append_insert_statement(sql, sizeof(sql), schema, "users", 200 + (int) i, name_two, 30 + (int) i, 0) == 0);
        }
        if (i % 4 == 0) {
            CHECK(append_select_projection_statement(sql, sizeof(sql), schema, "users", (i % 8) == 0) == 0);
        }
        CHECK(append_insert_statement(sql, sizeof(sql), schema, "users", 0, name_two, 99, 1) == 0);

        CHECK(execute_sql_script_to_file(sql, dir, output_path, &err) == SQL_FAILURE);
        CHECK(err.code == SQL_ERR_ARGUMENT);

        if (has_existing_csv) {
            csv_contents = read_text_file(csv_path);
            CHECK(csv_contents != NULL);
            CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
            free(csv_contents);
        } else {
            CHECK(path_exists(csv_path) == 0);
        }

        output_contents = read_text_file(output_path);
        CHECK(output_contents != NULL);
        CHECK(strcmp(output_contents, "") == 0);
        free(output_contents);

        cleanup_test_path(output_path);
        cleanup_test_path(schema_path);
        cleanup_test_path(csv_path);
        cleanup_test_dir(dir);
    }

    return 0;
}

static int test_execute_select_all_rejects_50_invalid_rows(void) {
    size_t i;

    for (i = 0; i < 50; ++i) {
        char dir[128];
        char csv_path[256];
        char schema_path[256];
        char output_path[256];
        char csv_contents_expected[1024];
        char sql[256];
        char *csv_contents_after;
        char *output_contents;
        const char *schema;
        SqlError err;

        schema = (i % 2 == 0) ? NULL : "app";
        build_invalid_csv_contents(csv_contents_expected, sizeof(csv_contents_expected), i);

        CHECK(create_test_dir(dir, sizeof(dir)) == 0);
        CHECK(write_standard_schema_file(dir, schema, "users", schema_path, sizeof(schema_path)) == 0);
        CHECK(write_standard_csv_file(dir,
                                      schema,
                                      "users",
                                      csv_path,
                                      sizeof(csv_path),
                                      csv_contents_expected) == 0);
        build_path(output_path, sizeof(output_path), dir, "output.txt");
        snprintf(sql,
                 sizeof(sql),
                 "SELECT * FROM %s%s%s;",
                 (schema != NULL) ? schema : "",
                 (schema != NULL) ? "." : "",
                 "users");

        CHECK(execute_sql_script_to_file(sql, dir, output_path, &err) == SQL_FAILURE);
        CHECK(err.code == SQL_ERR_PARSE);

        csv_contents_after = read_text_file(csv_path);
        output_contents = read_text_file(output_path);
        CHECK(csv_contents_after != NULL);
        CHECK(output_contents != NULL);
        CHECK(strcmp(csv_contents_after, csv_contents_expected) == 0);
        CHECK(strcmp(output_contents, "") == 0);

        free(csv_contents_after);
        free(output_contents);
        cleanup_test_path(output_path);
        cleanup_test_path(schema_path);
        cleanup_test_path(csv_path);
        cleanup_test_dir(dir);
    }

    return 0;
}

static int test_execute_script_output_failure_rolls_back_50_cases(void) {
    size_t i;

    for (i = 0; i < 50; ++i) {
        char dir[128];
        char csv_path[256];
        char schema_path[256];
        char output_path[256];
        char name_one[128];
        char name_two[128];
        char sql[4096];
        char *csv_contents;
        char *output_contents;
        char csv_file[128];
        FILE *output;
        const char *schema;
        int has_existing_csv;
        SqlError err;

        schema = (i % 2 == 0) ? NULL : "app";
        has_existing_csv = ((i / 2) % 2 == 0);
        build_case_name(name_one, sizeof(name_one), i + 200);
        build_case_name(name_two, sizeof(name_two), i + 300);

        CHECK(create_test_dir(dir, sizeof(dir)) == 0);
        CHECK(write_standard_schema_file(dir, schema, "users", schema_path, sizeof(schema_path)) == 0);
        if (has_existing_csv) {
            CHECK(write_standard_csv_file(dir,
                                          schema,
                                          "users",
                                          csv_path,
                                          sizeof(csv_path),
                                          "id,name,age\n1,\"alice\",20\n") == 0);
        } else {
            build_table_file_name(csv_file, sizeof(csv_file), schema, "users", ".csv");
            build_path(csv_path, sizeof(csv_path), dir, csv_file);
        }

        build_path(output_path, sizeof(output_path), dir, "output.txt");
        CHECK(write_text_file(output_path, "seed") == 0);

        sql[0] = '\0';
        CHECK(append_insert_statement(sql, sizeof(sql), schema, "users", 400 + (int) i, name_one, 40 + (int) i, 0) == 0);
        if (i % 3 == 0) {
            CHECK(append_insert_statement(sql, sizeof(sql), schema, "users", 500 + (int) i, name_two, 50 + (int) i, 0) == 0);
        }
        if (i % 5 == 0) {
            CHECK(append_select_projection_statement(sql, sizeof(sql), schema, "users", (i % 10) == 0) == 0);
        }

        output = fopen(output_path, "rb");
        CHECK(output != NULL);
        CHECK(execute_sql_script_to_stream(sql, dir, output, &err) == SQL_FAILURE);
        fclose(output);
        CHECK(err.code == SQL_ERR_IO);

        if (has_existing_csv) {
            csv_contents = read_text_file(csv_path);
            CHECK(csv_contents != NULL);
            CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
            free(csv_contents);
        } else {
            CHECK(path_exists(csv_path) == 0);
        }

        output_contents = read_text_file(output_path);
        CHECK(output_contents != NULL);
        CHECK(strcmp(output_contents, "seed") == 0);
        free(output_contents);

        cleanup_test_path(output_path);
        cleanup_test_path(schema_path);
        cleanup_test_path(csv_path);
        cleanup_test_dir(dir);
    }

    return 0;
}

static int test_execute_statement_output_failure_rolls_back_50_cases(void) {
    size_t i;

    for (i = 0; i < 50; ++i) {
        char dir[128];
        char csv_path[256];
        char schema_path[256];
        char output_path[256];
        char name_one[128];
        char sql[1024];
        char *csv_contents;
        char *output_contents;
        char csv_file[128];
        FILE *output;
        Statement stmt;
        const char *schema;
        int has_existing_csv;
        SqlError err;

        schema = (i % 2 == 0) ? NULL : "app";
        has_existing_csv = ((i / 2) % 2 == 0);
        build_case_name(name_one, sizeof(name_one), i + 400);

        CHECK(create_test_dir(dir, sizeof(dir)) == 0);
        CHECK(write_standard_schema_file(dir, schema, "users", schema_path, sizeof(schema_path)) == 0);
        if (has_existing_csv) {
            CHECK(write_standard_csv_file(dir,
                                          schema,
                                          "users",
                                          csv_path,
                                          sizeof(csv_path),
                                          "id,name,age\n1,\"alice\",20\n") == 0);
        } else {
            build_table_file_name(csv_file, sizeof(csv_file), schema, "users", ".csv");
            build_path(csv_path, sizeof(csv_path), dir, csv_file);
        }

        build_path(output_path, sizeof(output_path), dir, "output.txt");
        CHECK(write_text_file(output_path, "seed") == 0);

        sql[0] = '\0';
        CHECK(append_insert_statement(sql, sizeof(sql), schema, "users", 600 + (int) i, name_one, 60 + (int) i, 0) == 0);
        CHECK(parse_statement(sql, &stmt, &err) == SQL_SUCCESS);

        output = fopen(output_path, "rb");
        CHECK(output != NULL);
        CHECK(execute_statement(&stmt, dir, output, &err) == SQL_FAILURE);
        fclose(output);
        statement_free(&stmt);
        CHECK(err.code == SQL_ERR_IO);

        if (has_existing_csv) {
            csv_contents = read_text_file(csv_path);
            CHECK(csv_contents != NULL);
            CHECK(strcmp(csv_contents, "id,name,age\n1,\"alice\",20\n") == 0);
            free(csv_contents);
        } else {
            CHECK(path_exists(csv_path) == 0);
        }

        output_contents = read_text_file(output_path);
        CHECK(output_contents != NULL);
        CHECK(strcmp(output_contents, "seed") == 0);
        free(output_contents);

        cleanup_test_path(output_path);
        cleanup_test_path(schema_path);
        cleanup_test_path(csv_path);
        cleanup_test_dir(dir);
    }

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
        { "execute_select_all_pretty_ascii", test_execute_select_all_pretty_ascii },
        { "execute_select_projection_pretty_ascii", test_execute_select_projection_pretty_ascii },
        { "execute_select_pretty_empty_result", test_execute_select_pretty_empty_result },
        { "execute_script_insert_then_select_pretty_ascii", test_execute_script_insert_then_select_pretty_ascii },
        { "execute_select_pretty_strings_render_human_values", test_execute_select_pretty_strings_render_human_values },
        { "execute_script_rollback_on_failure", test_execute_script_rollback_on_failure },
        { "execute_select_all_rejects_invalid_row_shape", test_execute_select_all_rejects_invalid_row_shape },
        { "execute_output_failure_rolls_back_committed_data", test_execute_output_failure_rolls_back_committed_data },
        { "execute_pretty_output_failure_rolls_back_committed_data", test_execute_pretty_output_failure_rolls_back_committed_data },
        { "execute_script_rollback_on_failure_50_cases", test_execute_script_rollback_on_failure_50_cases },
        { "execute_select_all_rejects_50_invalid_rows", test_execute_select_all_rejects_50_invalid_rows },
        { "execute_script_output_failure_rolls_back_50_cases", test_execute_script_output_failure_rolls_back_50_cases },
        { "execute_statement_output_failure_rolls_back_50_cases", test_execute_statement_output_failure_rolls_back_50_cases }
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
