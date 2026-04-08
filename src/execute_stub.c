#include "execute.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err) {
    char *path;
    FILE *file;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);

    if (stmt == NULL || data_dir == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "stmt, data_dir, and out are required");
        return SQL_FAILURE;
    }

    if (stmt->table == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "Statement target table is required");
        return SQL_FAILURE;
    }

    if (stmt->type == STMT_INSERT) {
        if (storage_append_row(data_dir,
                               stmt->schema,
                               stmt->table,
                               stmt->values,
                               stmt->value_count,
                               err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (fprintf(out, "INSERT 1 INTO %s%s%s\n",
                    (stmt->schema != NULL) ? stmt->schema : "",
                    (stmt->schema != NULL) ? "." : "",
                    stmt->table) < 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write INSERT result");
            return SQL_FAILURE;
        }

        if (fflush(out) != 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to flush INSERT result");
            return SQL_FAILURE;
        }

        return SQL_SUCCESS;
    }

    if (stmt->type != STMT_SELECT) {
        sql_error_set(err, SQL_ERR_UNSUPPORTED, 0, "Unsupported statement type for execution");
        return SQL_FAILURE;
    }

    path = NULL;
    if (build_table_path(data_dir, stmt->schema, stmt->table, &path, err) != SQL_SUCCESS) {
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
