#include "execute.h"
#include "parse.h"
#include "sql_error.h"
#include "sql_types.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define SIZE_T_FMT "%Iu"
#else
#define SIZE_T_FMT "%zu"
#endif

/* SQL 파일 전체를 메모리에 올려 parser가 한 번에 읽을 수 있게 한다. */
static char *read_entire_file(const char *path, SqlError *err) {
    FILE *file;
    long size;
    size_t read_size;
    char *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open SQL file '%s'", path);
        return NULL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to seek SQL file '%s'", path);
        return NULL;
    }

    size = ftell(file);
    if (size < 0) {
        fclose(file);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to determine SQL file size '%s'", path);
        return NULL;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to rewind SQL file '%s'", path);
        return NULL;
    }

    buffer = (char *) malloc((size_t) size + 1);
    if (buffer == NULL) {
        fclose(file);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while reading SQL file '%s'", path);
        return NULL;
    }

    read_size = fread(buffer, 1u, (size_t) size, file);
    if (read_size != (size_t) size) {
        free(buffer);
        fclose(file);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to read entire SQL file '%s'", path);
        return NULL;
    }

    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, char **argv) {
    const char *sql_path;
    const char *data_dir;
    char *sql_text;
    Statement stmt;
    SqlError err;

#ifdef _WIN32
    /* Windows 콘솔에서 UTF-8 CSV/문자열을 그대로 볼 수 있게 코드 페이지를 맞춘다. */
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <sql_file> [data_dir]\n", argv[0]);
        return EXIT_FAILURE;
    }

    sql_path = argv[1];
    data_dir = (argc == 3) ? argv[2] : SQL_DEFAULT_DATA_DIR;

    sql_error_clear(&err);
    sql_text = read_entire_file(sql_path, &err);
    if (sql_text == NULL) {
        fprintf(stderr, "I/O error: %s\n", err.message);
        return EXIT_FAILURE;
    }

    statement_init(&stmt);
    if (parse_sql(sql_text, &stmt, &err) != SQL_SUCCESS) {
        fprintf(stderr, "Parse error [%s] at position " SIZE_T_FMT ": %s\n",
                sql_error_code_name(err.code),
                err.position,
                err.message);
        statement_free(&stmt);
        free(sql_text);
        return EXIT_FAILURE;
    }

    if (execute_statement(&stmt, data_dir, stdout, &err) != SQL_SUCCESS) {
        fprintf(stderr, "Execution error [%s]: %s\n",
                sql_error_code_name(err.code),
                err.message);
        statement_free(&stmt);
        free(sql_text);
        return EXIT_FAILURE;
    }

    statement_free(&stmt);
    free(sql_text);
    return EXIT_SUCCESS;
}
