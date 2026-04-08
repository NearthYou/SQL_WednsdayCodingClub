#include "storage.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define LONG_LONG_FMT "%I64d"
#else
#define LONG_LONG_FMT "%lld"
#endif

static int build_table_path(const char *data_dir,
                            const char *schema,
                            const char *table,
                            char **out,
                            SqlError *err) {
    size_t data_len;
    size_t schema_len;
    size_t table_len;
    size_t path_len;
    int needs_separator;
    char *path;
    char *cursor;

    if (data_dir == NULL || table == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "data_dir, table, and out are required");
        return SQL_FAILURE;
    }

    data_len = strlen(data_dir);
    schema_len = (schema != NULL) ? strlen(schema) : 0;
    table_len = strlen(table);
    needs_separator = (data_len > 0 && data_dir[data_len - 1] != '/' && data_dir[data_len - 1] != '\\');

    if (data_len > SIZE_MAX - table_len - 6) {
        sql_error_set(err, SQL_ERR_MEMORY, 0, "CSV path is too large to allocate safely");
        return SQL_FAILURE;
    }

    path_len = data_len + table_len + 5;
    if (needs_separator) {
        path_len++;
    }

    if (schema != NULL) {
        if (path_len > SIZE_MAX - schema_len - 2) {
            sql_error_set(err, SQL_ERR_MEMORY, 0, "CSV path is too large to allocate safely");
            return SQL_FAILURE;
        }
        path_len += schema_len + 2;
    }

    path = (char *) malloc(path_len + 1);
    if (path == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while building CSV path");
        return SQL_FAILURE;
    }

    cursor = path;
    if (data_len > 0) {
        memcpy(cursor, data_dir, data_len);
        cursor += data_len;
    }

    if (needs_separator) {
        *cursor++ = '/';
    }

    if (schema != NULL) {
        memcpy(cursor, schema, schema_len);
        cursor += schema_len;
        memcpy(cursor, "__", 2);
        cursor += 2;
    }

    memcpy(cursor, table, table_len);
    cursor += table_len;
    memcpy(cursor, ".csv", 4);
    cursor += 4;
    *cursor = '\0';

    *out = path;
    return SQL_SUCCESS;
}

static int ensure_header_exists(FILE *file, const char *path, SqlError *err) {
    int ch;

    if (fseek(file, 0L, SEEK_SET) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to seek CSV file '%s'", path);
        return SQL_FAILURE;
    }

    ch = fgetc(file);
    if (ch == EOF) {
        sql_error_set(err, SQL_ERR_PARSE, 0, "CSV file '%s' must contain a header row", path);
        return SQL_FAILURE;
    }

    if (ch == '\n' || ch == '\r') {
        sql_error_set(err, SQL_ERR_PARSE, 0, "CSV file '%s' must contain a non-empty header row", path);
        return SQL_FAILURE;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to rewind CSV file '%s'", path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int read_header_field_count(FILE *file,
                                   const char *path,
                                   size_t *field_count,
                                   SqlError *err) {
    int ch;
    size_t count;

    if (ensure_header_exists(file, path, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    ch = fgetc(file);
    count = 1;
    while (ch != EOF && ch != '\n' && ch != '\r') {
        if (ch == ',') {
            count++;
        }
        ch = fgetc(file);
    }

    *field_count = count;
    return SQL_SUCCESS;
}

static int file_ends_with_newline(FILE *file, const char *path, int *ends_with_newline, SqlError *err) {
    long file_size;
    int last_char;

    if (fseek(file, 0L, SEEK_END) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to seek CSV file '%s'", path);
        return SQL_FAILURE;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to determine CSV file size '%s'", path);
        return SQL_FAILURE;
    }

    if (file_size == 0) {
        *ends_with_newline = 1;
        return SQL_SUCCESS;
    }

    if (fseek(file, file_size - 1L, SEEK_SET) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to inspect CSV tail '%s'", path);
        return SQL_FAILURE;
    }

    last_char = fgetc(file);
    if (last_char == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to inspect CSV tail '%s'", path);
        return SQL_FAILURE;
    }

    *ends_with_newline = (last_char == '\n' || last_char == '\r');
    return SQL_SUCCESS;
}

static int write_escaped_string(FILE *file, const char *value, const char *path, SqlError *err) {
    size_t i;

    if (fputc('"', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV string to '%s'", path);
        return SQL_FAILURE;
    }

    for (i = 0; value[i] != '\0'; ++i) {
        if (value[i] == '"') {
            if (fputc('"', file) == EOF || fputc('"', file) == EOF) {
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV string to '%s'", path);
                return SQL_FAILURE;
            }
            continue;
        }

        if (fputc((unsigned char) value[i], file) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV string to '%s'", path);
            return SQL_FAILURE;
        }
    }

    if (fputc('"', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV string to '%s'", path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int write_csv_row(FILE *file,
                         const char *path,
                         const SqlValue *values,
                         size_t value_count,
                         SqlError *err) {
    size_t i;

    for (i = 0; i < value_count; ++i) {
        if (i > 0 && fputc(',', file) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV row to '%s'", path);
            return SQL_FAILURE;
        }

        if (values[i].type == SQL_VALUE_INT) {
            if (fprintf(file, LONG_LONG_FMT, values[i].as.int_value) < 0) {
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to write integer value to '%s'", path);
                return SQL_FAILURE;
            }
            continue;
        }

        if (values[i].type == SQL_VALUE_STRING) {
            if (write_escaped_string(file, values[i].as.string_value, path, err) != SQL_SUCCESS) {
                return SQL_FAILURE;
            }
            continue;
        }

        sql_error_set(err, SQL_ERR_UNSUPPORTED, 0, "Unsupported SQL value type for CSV write");
        return SQL_FAILURE;
    }

    if (fputc('\n', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish CSV row in '%s'", path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int stream_table(FILE *file, FILE *out, const char *path, SqlError *err) {
    char buffer[4096];
    size_t read_size;

    while ((read_size = fread(buffer, 1u, sizeof(buffer), file)) > 0) {
        if (fwrite(buffer, 1u, read_size, out) != read_size) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write SELECT output for '%s'", path);
            return SQL_FAILURE;
        }
    }

    if (ferror(file)) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to read CSV file '%s'", path);
        return SQL_FAILURE;
    }

    if (fflush(out) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to flush SELECT output for '%s'", path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

int storage_append_row(const char *data_dir,
                       const char *schema,
                       const char *table,
                       const SqlValue *values,
                       size_t value_count,
                       SqlError *err) {
    char *path;
    FILE *file;
    size_t header_field_count;
    int ends_with_newline;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);

    if (data_dir == NULL || table == NULL || values == NULL || value_count == 0) {
        sql_error_set(err,
                      SQL_ERR_ARGUMENT,
                      0,
                      "data_dir, table, values, and a non-empty value_count are required");
        return SQL_FAILURE;
    }

    path = NULL;
    if (build_table_path(data_dir, schema, table, &path, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    file = fopen(path, "rb+");
    if (file == NULL) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open CSV file '%s'", path);
        free(path);
        return SQL_FAILURE;
    }

    if (read_header_field_count(file, path, &header_field_count, err) != SQL_SUCCESS) {
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (header_field_count != value_count) {
        sql_error_set(err,
                      SQL_ERR_ARGUMENT,
                      0,
                      "CSV header expects %zu values but INSERT provided %zu",
                      header_field_count,
                      value_count);
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (file_ends_with_newline(file, path, &ends_with_newline, err) != SQL_SUCCESS) {
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to seek CSV file '%s'", path);
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (!ends_with_newline && fputc('\n', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to prepare CSV row append for '%s'", path);
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (write_csv_row(file, path, values, value_count, err) != SQL_SUCCESS) {
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (fflush(file) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to flush CSV file '%s'", path);
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    fclose(file);
    free(path);
    return SQL_SUCCESS;
}

int storage_select_all(const char *data_dir,
                       const char *schema,
                       const char *table,
                       FILE *out,
                       SqlError *err) {
    char *path;
    FILE *file;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);

    if (data_dir == NULL || table == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "data_dir, table, and out are required");
        return SQL_FAILURE;
    }

    path = NULL;
    if (build_table_path(data_dir, schema, table, &path, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open CSV file '%s' for SELECT", path);
        free(path);
        return SQL_FAILURE;
    }

    if (ensure_header_exists(file, path, err) != SQL_SUCCESS) {
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    if (stream_table(file, out, path, err) != SQL_SUCCESS) {
        fclose(file);
        free(path);
        return SQL_FAILURE;
    }

    fclose(file);
    free(path);
    return SQL_SUCCESS;
}
