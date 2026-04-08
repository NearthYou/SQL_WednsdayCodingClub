#include "storage.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum StorageColumnType {
    STORAGE_COL_INT = 0,
    STORAGE_COL_STRING
} StorageColumnType;

typedef struct SchemaColumn {
    char *name;
    StorageColumnType type;
} SchemaColumn;

typedef struct TableSchema {
    SchemaColumn *columns;
    size_t count;
} TableSchema;

typedef struct CsvRow {
    char **fields;
    size_t count;
} CsvRow;

static void csv_row_init(CsvRow *row) {
    if (row != NULL) {
        memset(row, 0, sizeof(*row));
    }
}

static void csv_row_free(CsvRow *row) {
    size_t i;

    if (row == NULL) {
        return;
    }

    for (i = 0; i < row->count; ++i) {
        free(row->fields[i]);
    }

    free(row->fields);
    csv_row_init(row);
}

static void table_schema_init(TableSchema *schema) {
    if (schema != NULL) {
        memset(schema, 0, sizeof(*schema));
    }
}

static void table_schema_free(TableSchema *schema) {
    size_t i;

    if (schema == NULL) {
        return;
    }

    for (i = 0; i < schema->count; ++i) {
        free(schema->columns[i].name);
    }

    free(schema->columns);
    table_schema_init(schema);
}

static int ascii_equals_ignore_case(const char *left, const char *right) {
    size_t i;

    if (left == NULL || right == NULL) {
        return 0;
    }

    for (i = 0; left[i] != '\0' && right[i] != '\0'; ++i) {
        if (tolower((unsigned char) left[i]) != tolower((unsigned char) right[i])) {
            return 0;
        }
    }

    return left[i] == '\0' && right[i] == '\0';
}

static char *copy_substring(const char *start, size_t length) {
    char *copy;

    copy = (char *) malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    if (length > 0) {
        memcpy(copy, start, length);
    }

    copy[length] = '\0';
    return copy;
}

static char *duplicate_string(const char *text) {
    if (text == NULL) {
        return NULL;
    }

    return copy_substring(text, strlen(text));
}

static void strip_utf8_bom_inplace(char *text) {
    size_t length;

    if (text == NULL) {
        return;
    }

    if ((unsigned char) text[0] == 0xEF &&
        (unsigned char) text[1] == 0xBB &&
        (unsigned char) text[2] == 0xBF) {
        length = strlen(text);
        memmove(text, text + 3, length - 2);
    }
}

static int append_char(char **buffer,
                       size_t *length,
                       size_t *capacity,
                       char ch,
                       SqlError *err,
                       const char *context,
                       const char *path) {
    char *expanded;
    size_t new_capacity;

    if (*length + 1 >= *capacity) {
        new_capacity = (*capacity == 0) ? 32 : (*capacity * 2);
        if (new_capacity <= *length + 1) {
            new_capacity = *length + 2;
        }

        expanded = (char *) realloc(*buffer, new_capacity);
        if (expanded == NULL) {
            sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while handling %s for '%s'", context, path);
            return SQL_FAILURE;
        }

        *buffer = expanded;
        *capacity = new_capacity;
    }

    (*buffer)[*length] = ch;
    (*length)++;
    (*buffer)[*length] = '\0';
    return SQL_SUCCESS;
}

static int append_field(CsvRow *row, char *field, SqlError *err, const char *path) {
    char **expanded;
    size_t new_count;

    if (row->count >= ((size_t) -1) / sizeof(*expanded)) {
        free(field);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "CSV row is too large to allocate safely for '%s'", path);
        return SQL_FAILURE;
    }

    new_count = row->count + 1;
    expanded = (char **) realloc(row->fields, new_count * sizeof(*expanded));
    if (expanded == NULL) {
        free(field);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while growing CSV row for '%s'", path);
        return SQL_FAILURE;
    }

    expanded[row->count] = field;
    row->fields = expanded;
    row->count = new_count;
    return SQL_SUCCESS;
}

static int append_schema_column(TableSchema *schema,
                                char *name,
                                StorageColumnType type,
                                SqlError *err,
                                const char *path) {
    SchemaColumn *expanded;
    size_t new_count;

    if (schema->count >= ((size_t) -1) / sizeof(*expanded)) {
        free(name);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Schema is too large to allocate safely for '%s'", path);
        return SQL_FAILURE;
    }

    new_count = schema->count + 1;
    expanded = (SchemaColumn *) realloc(schema->columns, new_count * sizeof(*expanded));
    if (expanded == NULL) {
        free(name);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while growing schema for '%s'", path);
        return SQL_FAILURE;
    }

    expanded[schema->count].name = name;
    expanded[schema->count].type = type;
    schema->columns = expanded;
    schema->count = new_count;
    return SQL_SUCCESS;
}

static int build_data_path(char **out,
                           const char *data_dir,
                           const char *schema,
                           const char *table,
                           const char *suffix,
                           SqlError *err) {
    size_t dir_len;
    size_t schema_len;
    size_t table_len;
    size_t suffix_len;
    size_t total_len;
    char *path;

    if (out == NULL || data_dir == NULL || table == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "data_dir and table are required");
        return SQL_FAILURE;
    }

    dir_len = strlen(data_dir);
    schema_len = (schema != NULL) ? strlen(schema) : 0;
    table_len = strlen(table);
    suffix_len = (suffix != NULL) ? strlen(suffix) : 0;
    total_len = dir_len + 1 + table_len + suffix_len + 1;
    if (schema != NULL) {
        total_len += schema_len + 2;
    }

    path = (char *) malloc(total_len);
    if (path == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while building data path");
        return SQL_FAILURE;
    }

    if (schema != NULL) {
        snprintf(path, total_len, "%s/%s__%s%s", data_dir, schema, table, (suffix != NULL) ? suffix : "");
    } else {
        snprintf(path, total_len, "%s/%s%s", data_dir, table, (suffix != NULL) ? suffix : "");
    }

    *out = path;
    return SQL_SUCCESS;
}

static int find_schema_index(const TableSchema *schema, const char *name) {
    size_t i;

    if (schema == NULL || name == NULL) {
        return -1;
    }

    for (i = 0; i < schema->count; ++i) {
        if (strcmp(schema->columns[i].name, name) == 0) {
            return (int) i;
        }
    }

    return -1;
}

static int file_exists(const char *path) {
    FILE *file;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    return 1;
}

/* CSV 문자열 안의 개행도 한 레코드로 읽어야 해서 줄바꿈은 quote 바깥에서만 종료로 본다. */
static int read_csv_record(FILE *file, char **out, SqlError *err, const char *path) {
    char *buffer;
    size_t length;
    size_t capacity;
    int in_quotes;
    int saw_any;

    buffer = NULL;
    length = 0;
    capacity = 0;
    in_quotes = 0;
    saw_any = 0;

    while (1) {
        int ch;

        ch = fgetc(file);
        if (ch == EOF) {
            if (ferror(file)) {
                free(buffer);
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to read CSV record from '%s'", path);
                return -1;
            }

            break;
        }

        saw_any = 1;
        if (ch == '"') {
            int next;

            if (append_char(&buffer, &length, &capacity, (char) ch, err, "CSV record", path) != SQL_SUCCESS) {
                free(buffer);
                return -1;
            }

            next = fgetc(file);
            if (in_quotes && next == '"') {
                if (append_char(&buffer, &length, &capacity, '"', err, "CSV record", path) != SQL_SUCCESS) {
                    free(buffer);
                    return -1;
                }
                continue;
            }

            if (next != EOF) {
                ungetc(next, file);
            }

            in_quotes = !in_quotes;
            continue;
        }

        if (!in_quotes && (ch == '\n' || ch == '\r')) {
            if (ch == '\r') {
                int next;

                next = fgetc(file);
                if (next != '\n' && next != EOF) {
                    ungetc(next, file);
                }
            }
            break;
        }

        if (append_char(&buffer, &length, &capacity, (char) ch, err, "CSV record", path) != SQL_SUCCESS) {
            free(buffer);
            return -1;
        }
    }

    if (!saw_any) {
        free(buffer);
        *out = NULL;
        return 0;
    }

    if (buffer == NULL) {
        buffer = duplicate_string("");
        if (buffer == NULL) {
            sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while finishing CSV record for '%s'", path);
            return -1;
        }
    }

    if (in_quotes) {
        free(buffer);
        sql_error_set(err, SQL_ERR_PARSE, 0, "Unterminated quoted CSV field in '%s'", path);
        return -1;
    }

    *out = buffer;
    return 1;
}

static int parse_csv_record(const char *record, CsvRow *row, SqlError *err, const char *path) {
    size_t pos;

    csv_row_init(row);
    pos = 0;

    while (1) {
        char *field;

        if (record[pos] == '"') {
            char *buffer;
            size_t length;
            size_t capacity;

            pos++;
            buffer = NULL;
            length = 0;
            capacity = 0;
            while (1) {
                char ch;

                ch = record[pos];
                if (ch == '\0') {
                    free(buffer);
                    csv_row_free(row);
                    sql_error_set(err, SQL_ERR_PARSE, 0, "Unterminated quoted CSV field in '%s'", path);
                    return SQL_FAILURE;
                }

                if (ch == '"') {
                    if (record[pos + 1] == '"') {
                        if (append_char(&buffer,
                                        &length,
                                        &capacity,
                                        '"',
                                        err,
                                        "CSV field",
                                        path) != SQL_SUCCESS) {
                            free(buffer);
                            csv_row_free(row);
                            return SQL_FAILURE;
                        }
                        pos += 2;
                        continue;
                    }

                    pos++;
                    break;
                }

                if (append_char(&buffer,
                                &length,
                                &capacity,
                                ch,
                                err,
                                "CSV field",
                                path) != SQL_SUCCESS) {
                    free(buffer);
                    csv_row_free(row);
                    return SQL_FAILURE;
                }
                pos++;
            }

            if (buffer == NULL) {
                buffer = duplicate_string("");
                if (buffer == NULL) {
                    csv_row_free(row);
                    sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while finishing CSV field for '%s'", path);
                    return SQL_FAILURE;
                }
            }

            if (record[pos] != '\0' && record[pos] != ',') {
                free(buffer);
                csv_row_free(row);
                sql_error_set(err, SQL_ERR_PARSE, 0, "Invalid character after quoted CSV field in '%s'", path);
                return SQL_FAILURE;
            }

            field = buffer;
        } else {
            size_t start;

            start = pos;
            while (record[pos] != '\0' && record[pos] != ',') {
                pos++;
            }

            field = copy_substring(record + start, pos - start);
            if (field == NULL) {
                csv_row_free(row);
                sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while copying CSV field for '%s'", path);
                return SQL_FAILURE;
            }
        }

        if (row->count == 0) {
            strip_utf8_bom_inplace(field);
        }

        if (append_field(row, field, err, path) != SQL_SUCCESS) {
            csv_row_free(row);
            return SQL_FAILURE;
        }

        if (record[pos] == '\0') {
            break;
        }

        pos++;
        if (record[pos] == '\0') {
            field = duplicate_string("");
            if (field == NULL) {
                csv_row_free(row);
                sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while finishing trailing CSV field for '%s'", path);
                return SQL_FAILURE;
            }

            if (append_field(row, field, err, path) != SQL_SUCCESS) {
                csv_row_free(row);
                return SQL_FAILURE;
            }
            break;
        }
    }

    return SQL_SUCCESS;
}

static int parse_schema_type(const char *text, StorageColumnType *out_type) {
    if (ascii_equals_ignore_case(text, "INT")) {
        *out_type = STORAGE_COL_INT;
        return SQL_SUCCESS;
    }

    if (ascii_equals_ignore_case(text, "STRING")) {
        *out_type = STORAGE_COL_STRING;
        return SQL_SUCCESS;
    }

    return SQL_FAILURE;
}

static int load_table_schema(const char *data_dir,
                             const char *schema_name,
                             const char *table,
                             TableSchema *out_schema,
                             int *has_schema,
                             SqlError *err) {
    char *schema_path;
    FILE *file;
    char *record;
    int read_result;
    CsvRow row;
    TableSchema schema;

    schema_path = NULL;
    file = NULL;
    record = NULL;
    csv_row_init(&row);
    table_schema_init(&schema);

    if (build_data_path(&schema_path, data_dir, schema_name, table, ".schema.csv", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    file = fopen(schema_path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            *has_schema = 0;
            free(schema_path);
            return SQL_SUCCESS;
        }

        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open schema file '%s'", schema_path);
        free(schema_path);
        return SQL_FAILURE;
    }

    read_result = read_csv_record(file, &record, err, schema_path);
    if (read_result <= 0) {
        fclose(file);
        if (read_result == 0) {
            sql_error_set(err, SQL_ERR_PARSE, 0, "Schema file '%s' must contain a header row", schema_path);
        }
        free(schema_path);
        return SQL_FAILURE;
    }

    if (parse_csv_record(record, &row, err, schema_path) != SQL_SUCCESS) {
        fclose(file);
        free(record);
        free(schema_path);
        return SQL_FAILURE;
    }

    if (row.count != 2 || strcmp(row.fields[0], "name") != 0 || strcmp(row.fields[1], "type") != 0) {
        csv_row_free(&row);
        fclose(file);
        free(record);
        sql_error_set(err, SQL_ERR_PARSE, 0, "Schema file '%s' must start with header 'name,type'", schema_path);
        free(schema_path);
        return SQL_FAILURE;
    }

    csv_row_free(&row);
    free(record);
    record = NULL;

    while ((read_result = read_csv_record(file, &record, err, schema_path)) > 0) {
        StorageColumnType column_type;
        char *name_copy;

        if (parse_csv_record(record, &row, err, schema_path) != SQL_SUCCESS) {
            fclose(file);
            free(record);
            free(schema_path);
            table_schema_free(&schema);
            return SQL_FAILURE;
        }

        if (row.count != 2 || row.fields[0][0] == '\0') {
            csv_row_free(&row);
            fclose(file);
            free(record);
            sql_error_set(err, SQL_ERR_PARSE, 0, "Each schema row in '%s' must contain 'name,type'", schema_path);
            free(schema_path);
            table_schema_free(&schema);
            return SQL_FAILURE;
        }

        if (parse_schema_type(row.fields[1], &column_type) != SQL_SUCCESS) {
            csv_row_free(&row);
            fclose(file);
            free(record);
            sql_error_set(err, SQL_ERR_PARSE, 0, "Unsupported schema type '%s' in '%s'", row.fields[1], schema_path);
            free(schema_path);
            table_schema_free(&schema);
            return SQL_FAILURE;
        }

        if (find_schema_index(&schema, row.fields[0]) >= 0) {
            csv_row_free(&row);
            fclose(file);
            free(record);
            sql_error_set(err, SQL_ERR_PARSE, 0, "Duplicate schema column '%s' in '%s'", row.fields[0], schema_path);
            free(schema_path);
            table_schema_free(&schema);
            return SQL_FAILURE;
        }

        name_copy = duplicate_string(row.fields[0]);
        if (name_copy == NULL) {
            csv_row_free(&row);
            fclose(file);
            free(record);
            sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while loading schema '%s'", schema_path);
            free(schema_path);
            table_schema_free(&schema);
            return SQL_FAILURE;
        }

        if (append_schema_column(&schema, name_copy, column_type, err, schema_path) != SQL_SUCCESS) {
            csv_row_free(&row);
            fclose(file);
            free(record);
            free(schema_path);
            table_schema_free(&schema);
            return SQL_FAILURE;
        }

        csv_row_free(&row);
        free(record);
        record = NULL;
    }

    fclose(file);
    free(schema_path);
    if (read_result < 0) {
        table_schema_free(&schema);
        return SQL_FAILURE;
    }

    if (schema.count == 0) {
        table_schema_free(&schema);
        sql_error_set(err, SQL_ERR_PARSE, 0, "Schema file must define at least one column");
        return SQL_FAILURE;
    }

    *out_schema = schema;
    *has_schema = 1;
    return SQL_SUCCESS;
}

static int load_csv_header(const char *csv_path,
                           CsvRow *header_row,
                           FILE **out_file,
                           SqlError *err) {
    FILE *file;
    char *record;
    int read_result;

    file = fopen(csv_path, "rb");
    if (file == NULL) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open CSV file '%s'", csv_path);
        return SQL_FAILURE;
    }

    record = NULL;
    read_result = read_csv_record(file, &record, err, csv_path);
    if (read_result <= 0) {
        fclose(file);
        if (read_result == 0) {
            sql_error_set(err, SQL_ERR_PARSE, 0, "CSV file '%s' must contain a header row", csv_path);
        }
        return SQL_FAILURE;
    }

    if (parse_csv_record(record, header_row, err, csv_path) != SQL_SUCCESS) {
        fclose(file);
        free(record);
        return SQL_FAILURE;
    }

    free(record);
    if (header_row->count == 0) {
        csv_row_free(header_row);
        fclose(file);
        sql_error_set(err, SQL_ERR_PARSE, 0, "CSV file '%s' must contain at least one header column", csv_path);
        return SQL_FAILURE;
    }

    *out_file = file;
    return SQL_SUCCESS;
}

static int header_matches_schema(const CsvRow *header_row,
                                 const TableSchema *schema,
                                 const char *csv_path,
                                 SqlError *err) {
    size_t i;

    if (header_row->count != schema->count) {
        sql_error_set(err,
                      SQL_ERR_PARSE,
                      0,
                      "CSV header in '%s' has %u columns but schema expects %u",
                      csv_path,
                      (unsigned) header_row->count,
                      (unsigned) schema->count);
        return SQL_FAILURE;
    }

    for (i = 0; i < schema->count; ++i) {
        if (strcmp(header_row->fields[i], schema->columns[i].name) != 0) {
            sql_error_set(err,
                          SQL_ERR_PARSE,
                          0,
                          "CSV header in '%s' does not match schema column '%s' at position %u",
                          csv_path,
                          schema->columns[i].name,
                          (unsigned) (i + 1));
            return SQL_FAILURE;
        }
    }

    return SQL_SUCCESS;
}

static int write_csv_string(FILE *file, const char *text, SqlError *err, const char *csv_path) {
    size_t i;

    if (fputc('"', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV string to '%s'", csv_path);
        return SQL_FAILURE;
    }

    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '"') {
            if (fputc('"', file) == EOF || fputc('"', file) == EOF) {
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to escape CSV string for '%s'", csv_path);
                return SQL_FAILURE;
            }
        } else if (fputc((unsigned char) text[i], file) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV string to '%s'", csv_path);
            return SQL_FAILURE;
        }
    }

    if (fputc('"', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish CSV string in '%s'", csv_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int write_signed_integer(FILE *file, long long value, SqlError *err, const char *csv_path) {
    unsigned long long magnitude;
    char digits[32];
    size_t count;

    if (value < 0) {
        if (fputc('-', file) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write integer sign to '%s'", csv_path);
            return SQL_FAILURE;
        }

        if (value == LLONG_MIN) {
            magnitude = ((unsigned long long) LLONG_MAX) + 1u;
        } else {
            magnitude = (unsigned long long) (-value);
        }
    } else {
        magnitude = (unsigned long long) value;
    }

    count = 0;
    do {
        digits[count++] = (char) ('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude > 0);

    while (count > 0) {
        count--;
        if (fputc(digits[count], file) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write integer digits to '%s'", csv_path);
            return SQL_FAILURE;
        }
    }

    return SQL_SUCCESS;
}

static int write_csv_value(FILE *file,
                           const SqlValue *value,
                           const SchemaColumn *column,
                           SqlError *err,
                           const char *csv_path) {
    if (column != NULL && column->type == STORAGE_COL_STRING) {
        return write_csv_string(file, value->as.string_value, err, csv_path);
    }

    if (value->type == SQL_VALUE_STRING) {
        return write_csv_string(file, value->as.string_value, err, csv_path);
    }

    return write_signed_integer(file, value->as.int_value, err, csv_path);
}

static int append_newline_if_needed(FILE *file, const char *csv_path, SqlError *err) {
    long end_pos;

    if (fseek(file, 0L, SEEK_END) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to seek CSV file '%s' before append", csv_path);
        return SQL_FAILURE;
    }

    end_pos = ftell(file);
    if (end_pos < 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to determine CSV size for '%s'", csv_path);
        return SQL_FAILURE;
    }

    if (end_pos > 0) {
        int last;

        if (fseek(file, -1L, SEEK_END) != 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to inspect CSV tail for '%s'", csv_path);
            return SQL_FAILURE;
        }

        last = fgetc(file);
        if (last == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to read CSV tail for '%s'", csv_path);
            return SQL_FAILURE;
        }

        if (last != '\n' && last != '\r') {
            if (fseek(file, 0L, SEEK_END) != 0 || fputc('\n', file) == EOF) {
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to normalize CSV newline for '%s'", csv_path);
                return SQL_FAILURE;
            }
        } else if (fseek(file, 0L, SEEK_END) != 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to seek CSV end for '%s'", csv_path);
            return SQL_FAILURE;
        }
    }

    return SQL_SUCCESS;
}

static int append_row_to_file(FILE *file,
                              const char *csv_path,
                              const TableSchema *schema,
                              const SqlValue *values,
                              size_t value_count,
                              SqlError *err) {
    size_t i;

    if (append_newline_if_needed(file, csv_path, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    for (i = 0; i < value_count; ++i) {
        if (i > 0 && fputc(',', file) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write CSV delimiter to '%s'", csv_path);
            return SQL_FAILURE;
        }

        if (write_csv_value(file,
                            &values[i],
                            (schema != NULL) ? &schema->columns[i] : NULL,
                            err,
                            csv_path) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }
    }

    if (fputc('\n', file) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish CSV row in '%s'", csv_path);
        return SQL_FAILURE;
    }

    if (fflush(file) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to flush CSV file '%s'", csv_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int create_csv_from_schema(const char *csv_path, const TableSchema *schema, SqlError *err) {
    FILE *file;
    size_t i;
    int failed;

    file = fopen(csv_path, "wb");
    if (file == NULL) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to create CSV file '%s'", csv_path);
        return SQL_FAILURE;
    }

    failed = 0;
    for (i = 0; i < schema->count; ++i) {
        if (i > 0 && fputc(',', file) == EOF) {
            failed = 1;
            break;
        }

        if (fputs(schema->columns[i].name, file) == EOF) {
            failed = 1;
            break;
        }
    }

    if (!failed && fputc('\n', file) == EOF) {
        failed = 1;
    }

    if (fclose(file) != 0) {
        failed = 1;
    }

    if (failed) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to create header row in '%s'", csv_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int validate_value_types(const TableSchema *schema,
                                const SqlValue *values,
                                const char *csv_path,
                                SqlError *err) {
    size_t i;

    for (i = 0; i < schema->count; ++i) {
        if (schema->columns[i].type == STORAGE_COL_INT && values[i].type != SQL_VALUE_INT) {
            sql_error_set(err,
                          SQL_ERR_ARGUMENT,
                          0,
                          "Column '%s' in '%s' expects INT",
                          schema->columns[i].name,
                          csv_path);
            return SQL_FAILURE;
        }

        if (schema->columns[i].type == STORAGE_COL_STRING && values[i].type != SQL_VALUE_STRING) {
            sql_error_set(err,
                          SQL_ERR_ARGUMENT,
                          0,
                          "Column '%s' in '%s' expects STRING",
                          schema->columns[i].name,
                          csv_path);
            return SQL_FAILURE;
        }
    }

    return SQL_SUCCESS;
}

static int map_values_to_schema(const TableSchema *schema,
                                const char *const *column_names,
                                size_t column_count,
                                const SqlValue *values,
                                size_t value_count,
                                const char *csv_path,
                                SqlValue **out_values,
                                SqlError *err) {
    SqlValue *ordered_values;
    unsigned char *assigned;
    size_t i;

    if (value_count != schema->count) {
        sql_error_set(err,
                      SQL_ERR_ARGUMENT,
                      0,
                      "CSV table '%s' expects %u values but received %u",
                      csv_path,
                      (unsigned) schema->count,
                      (unsigned) value_count);
        return SQL_FAILURE;
    }

    ordered_values = (SqlValue *) calloc(schema->count, sizeof(*ordered_values));
    assigned = (unsigned char *) calloc(schema->count, sizeof(*assigned));
    if (ordered_values == NULL || assigned == NULL) {
        free(ordered_values);
        free(assigned);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while mapping values for '%s'", csv_path);
        return SQL_FAILURE;
    }

    if (column_count == 0) {
        for (i = 0; i < schema->count; ++i) {
            ordered_values[i] = values[i];
        }
    } else {
        if (column_count != schema->count) {
            free(ordered_values);
            free(assigned);
            sql_error_set(err,
                          SQL_ERR_ARGUMENT,
                          0,
                          "Explicit column INSERT into '%s' must list all %u schema columns",
                          csv_path,
                          (unsigned) schema->count);
            return SQL_FAILURE;
        }

        for (i = 0; i < column_count; ++i) {
            int schema_index;

            schema_index = find_schema_index(schema, column_names[i]);
            if (schema_index < 0) {
                free(ordered_values);
                free(assigned);
                sql_error_set(err,
                              SQL_ERR_ARGUMENT,
                              0,
                              "Unknown column '%s' for table '%s'",
                              column_names[i],
                              csv_path);
                return SQL_FAILURE;
            }

            if (assigned[schema_index]) {
                free(ordered_values);
                free(assigned);
                sql_error_set(err,
                              SQL_ERR_ARGUMENT,
                              0,
                              "Duplicate column '%s' in INSERT for '%s'",
                              column_names[i],
                              csv_path);
                return SQL_FAILURE;
            }

            assigned[schema_index] = 1;
            ordered_values[schema_index] = values[i];
        }
    }

    free(assigned);
    *out_values = ordered_values;
    return SQL_SUCCESS;
}

static int copy_stream(FILE *input, FILE *out, const char *csv_path, SqlError *err) {
    char buffer[4096];
    size_t read_size;

    while ((read_size = fread(buffer, 1u, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1u, read_size, out) != read_size) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write SELECT result for '%s'", csv_path);
            return SQL_FAILURE;
        }
    }

    if (ferror(input)) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to read CSV file '%s' for SELECT", csv_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int write_projection_row(FILE *out,
                                const TableSchema *schema,
                                const CsvRow *row,
                                const size_t *selected_indices,
                                size_t selected_count,
                                SqlError *err,
                                const char *csv_path) {
    size_t i;

    for (i = 0; i < selected_count; ++i) {
        size_t source_index;
        const char *field;
        StorageColumnType type;

        source_index = selected_indices[i];
        field = row->fields[source_index];
        type = schema->columns[source_index].type;

        if (i > 0 && fputc(',', out) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write projected CSV delimiter for '%s'", csv_path);
            return SQL_FAILURE;
        }

        if (type == STORAGE_COL_STRING) {
            if (write_csv_string(out, field, err, csv_path) != SQL_SUCCESS) {
                return SQL_FAILURE;
            }
        } else if (fputs(field, out) == EOF) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write projected INT field for '%s'", csv_path);
            return SQL_FAILURE;
        }
    }

    if (fputc('\n', out) == EOF) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish projected row for '%s'", csv_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

int storage_append_row(const char *data_dir,
                       const char *schema_name,
                       const char *table,
                       const char *const *column_names,
                       size_t column_count,
                       const SqlValue *values,
                       size_t value_count,
                       SqlError *err) {
    char *csv_path;
    TableSchema schema;
    int has_schema;
    CsvRow header_row;
    FILE *file;
    SqlValue *ordered_values;
    int created_csv;
    int result;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (data_dir == NULL || table == NULL || values == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "data_dir, table, and values are required");
        return SQL_FAILURE;
    }

    csv_path = NULL;
    ordered_values = NULL;
    file = NULL;
    created_csv = 0;
    has_schema = 0;
    csv_row_init(&header_row);
    table_schema_init(&schema);

    if (build_data_path(&csv_path, data_dir, schema_name, table, ".csv", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (load_table_schema(data_dir, schema_name, table, &schema, &has_schema, err) != SQL_SUCCESS) {
        free(csv_path);
        return SQL_FAILURE;
    }

    if (has_schema) {
        if (map_values_to_schema(&schema,
                                 column_names,
                                 column_count,
                                 values,
                                 value_count,
                                 csv_path,
                                 &ordered_values,
                                 err) != SQL_SUCCESS) {
            table_schema_free(&schema);
            free(csv_path);
            return SQL_FAILURE;
        }

        if (validate_value_types(&schema, ordered_values, csv_path, err) != SQL_SUCCESS) {
            free(ordered_values);
            table_schema_free(&schema);
            free(csv_path);
            return SQL_FAILURE;
        }

        if (!file_exists(csv_path)) {
            if (column_count == 0) {
                free(ordered_values);
                table_schema_free(&schema);
                sql_error_set(err,
                              SQL_ERR_IO,
                              0,
                              "Missing CSV file '%s'; auto-create requires an explicit full column list",
                              csv_path);
                free(csv_path);
                return SQL_FAILURE;
            }

            if (create_csv_from_schema(csv_path, &schema, err) != SQL_SUCCESS) {
                free(ordered_values);
                table_schema_free(&schema);
                free(csv_path);
                return SQL_FAILURE;
            }
            created_csv = 1;
        }
    } else {
        if (column_count > 0) {
            table_schema_free(&schema);
            sql_error_set(err,
                          SQL_ERR_UNSUPPORTED,
                          0,
                          "Column-name INSERT requires a schema file for '%s'",
                          csv_path);
            free(csv_path);
            return SQL_FAILURE;
        }

        if (!file_exists(csv_path)) {
            table_schema_free(&schema);
            sql_error_set(err, SQL_ERR_IO, 0, "Missing CSV file '%s' for INSERT", csv_path);
            free(csv_path);
            return SQL_FAILURE;
        }
    }

    if (load_csv_header(csv_path, &header_row, &file, err) != SQL_SUCCESS) {
        free(ordered_values);
        table_schema_free(&schema);
        free(csv_path);
        return SQL_FAILURE;
    }

    if (has_schema) {
        if (header_matches_schema(&header_row, &schema, csv_path, err) != SQL_SUCCESS) {
            csv_row_free(&header_row);
            fclose(file);
            free(ordered_values);
            table_schema_free(&schema);
            free(csv_path);
            return SQL_FAILURE;
        }
    } else if (header_row.count != value_count) {
        csv_row_free(&header_row);
        fclose(file);
        sql_error_set(err,
                      SQL_ERR_ARGUMENT,
                      0,
                      "CSV table '%s' expects %u values but received %u",
                      csv_path,
                      (unsigned) header_row.count,
                      (unsigned) value_count);
        table_schema_free(&schema);
        free(csv_path);
        return SQL_FAILURE;
    }

    csv_row_free(&header_row);
    if (fclose(file) != 0) {
        free(ordered_values);
        table_schema_free(&schema);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to close CSV file '%s' after header validation", csv_path);
        free(csv_path);
        return SQL_FAILURE;
    }

    file = fopen(csv_path, "r+b");
    if (file == NULL) {
        free(ordered_values);
        table_schema_free(&schema);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to reopen CSV file '%s' for append", csv_path);
        free(csv_path);
        return SQL_FAILURE;
    }

    result = append_row_to_file(file,
                                csv_path,
                                has_schema ? &schema : NULL,
                                has_schema ? ordered_values : values,
                                has_schema ? schema.count : value_count,
                                err);

    fclose(file);
    free(ordered_values);
    table_schema_free(&schema);
    free(csv_path);
    (void) created_csv;
    return result;
}

int storage_select_projection(const char *data_dir,
                              const char *schema_name,
                              const char *table,
                              const char *const *column_names,
                              size_t column_count,
                              int select_all,
                              FILE *out,
                              SqlError *err) {
    char *csv_path;
    TableSchema schema;
    int has_schema;
    CsvRow header_row;
    FILE *file;
    int result;
    size_t *selected_indices;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (data_dir == NULL || table == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "data_dir, table, and out are required");
        return SQL_FAILURE;
    }

    csv_path = NULL;
    file = NULL;
    selected_indices = NULL;
    has_schema = 0;
    csv_row_init(&header_row);
    table_schema_init(&schema);

    if (build_data_path(&csv_path, data_dir, schema_name, table, ".csv", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (load_table_schema(data_dir, schema_name, table, &schema, &has_schema, err) != SQL_SUCCESS) {
        free(csv_path);
        return SQL_FAILURE;
    }

    if (load_csv_header(csv_path, &header_row, &file, err) != SQL_SUCCESS) {
        table_schema_free(&schema);
        free(csv_path);
        return SQL_FAILURE;
    }

    if (has_schema && header_matches_schema(&header_row, &schema, csv_path, err) != SQL_SUCCESS) {
        csv_row_free(&header_row);
        fclose(file);
        table_schema_free(&schema);
        free(csv_path);
        return SQL_FAILURE;
    }

    if (select_all) {
        if (fseek(file, 0L, SEEK_SET) != 0) {
            csv_row_free(&header_row);
            fclose(file);
            table_schema_free(&schema);
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to rewind CSV file '%s' for SELECT", csv_path);
            free(csv_path);
            return SQL_FAILURE;
        }

        result = copy_stream(file, out, csv_path, err);
        csv_row_free(&header_row);
        fclose(file);
        table_schema_free(&schema);
        free(csv_path);
        return result;
    }

    if (!has_schema) {
        csv_row_free(&header_row);
        fclose(file);
        table_schema_free(&schema);
        sql_error_set(err,
                      SQL_ERR_UNSUPPORTED,
                      0,
                      "Column projection requires a schema file for '%s'",
                      csv_path);
        free(csv_path);
        return SQL_FAILURE;
    }

    if (column_names == NULL || column_count == 0) {
        csv_row_free(&header_row);
        fclose(file);
        table_schema_free(&schema);
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "SELECT projection requires at least one column");
        free(csv_path);
        return SQL_FAILURE;
    }

    selected_indices = (size_t *) malloc(column_count * sizeof(*selected_indices));
    if (selected_indices == NULL) {
        csv_row_free(&header_row);
        fclose(file);
        table_schema_free(&schema);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while preparing SELECT projection for '%s'", csv_path);
        free(csv_path);
        return SQL_FAILURE;
    }

    {
        size_t i;
        size_t j;

        for (i = 0; i < column_count; ++i) {
            int schema_index;

            schema_index = find_schema_index(&schema, column_names[i]);
            if (schema_index < 0) {
                free(selected_indices);
                csv_row_free(&header_row);
                fclose(file);
                table_schema_free(&schema);
                sql_error_set(err, SQL_ERR_ARGUMENT, 0, "Unknown SELECT column '%s' for '%s'", column_names[i], csv_path);
                free(csv_path);
                return SQL_FAILURE;
            }

            for (j = 0; j < i; ++j) {
                if (selected_indices[j] == (size_t) schema_index) {
                    free(selected_indices);
                    csv_row_free(&header_row);
                    fclose(file);
                    table_schema_free(&schema);
                    sql_error_set(err, SQL_ERR_ARGUMENT, 0, "Duplicate SELECT column '%s' for '%s'", column_names[i], csv_path);
                    free(csv_path);
                    return SQL_FAILURE;
                }
            }

            selected_indices[i] = (size_t) schema_index;
        }
    }

    {
        size_t i;

        for (i = 0; i < column_count; ++i) {
            if (i > 0 && fputc(',', out) == EOF) {
                free(selected_indices);
                csv_row_free(&header_row);
                fclose(file);
                table_schema_free(&schema);
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to write projected header for '%s'", csv_path);
                free(csv_path);
                return SQL_FAILURE;
            }

            if (fputs(column_names[i], out) == EOF) {
                free(selected_indices);
                csv_row_free(&header_row);
                fclose(file);
                table_schema_free(&schema);
                sql_error_set(err, SQL_ERR_IO, 0, "Failed to write projected header for '%s'", csv_path);
                free(csv_path);
                return SQL_FAILURE;
            }
        }

        if (fputc('\n', out) == EOF) {
            free(selected_indices);
            csv_row_free(&header_row);
            fclose(file);
            table_schema_free(&schema);
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish projected header for '%s'", csv_path);
            free(csv_path);
            return SQL_FAILURE;
        }
    }

    while (1) {
        char *record;
        CsvRow row;
        int read_result;

        record = NULL;
        csv_row_init(&row);
        read_result = read_csv_record(file, &record, err, csv_path);
        if (read_result < 0) {
            free(selected_indices);
            csv_row_free(&header_row);
            fclose(file);
            table_schema_free(&schema);
            free(csv_path);
            return SQL_FAILURE;
        }

        if (read_result == 0) {
            break;
        }

        if (parse_csv_record(record, &row, err, csv_path) != SQL_SUCCESS) {
            free(record);
            free(selected_indices);
            csv_row_free(&header_row);
            fclose(file);
            table_schema_free(&schema);
            free(csv_path);
            return SQL_FAILURE;
        }
        free(record);

        if (row.count != header_row.count) {
            csv_row_free(&row);
            free(selected_indices);
            csv_row_free(&header_row);
            fclose(file);
            table_schema_free(&schema);
            sql_error_set(err,
                          SQL_ERR_PARSE,
                          0,
                          "CSV row in '%s' has %u columns but header has %u",
                          csv_path,
                          (unsigned) row.count,
                          (unsigned) header_row.count);
            free(csv_path);
            return SQL_FAILURE;
        }

        if (write_projection_row(out, &schema, &row, selected_indices, column_count, err, csv_path) != SQL_SUCCESS) {
            csv_row_free(&row);
            free(selected_indices);
            csv_row_free(&header_row);
            fclose(file);
            table_schema_free(&schema);
            free(csv_path);
            return SQL_FAILURE;
        }

        csv_row_free(&row);
    }

    if (fflush(out) != 0) {
        free(selected_indices);
        csv_row_free(&header_row);
        fclose(file);
        table_schema_free(&schema);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to flush SELECT output for '%s'", csv_path);
        free(csv_path);
        return SQL_FAILURE;
    }

    free(selected_indices);
    csv_row_free(&header_row);
    fclose(file);
    table_schema_free(&schema);
    free(csv_path);
    return SQL_SUCCESS;
}

int storage_select_all(const char *data_dir,
                       const char *schema_name,
                       const char *table,
                       FILE *out,
                       SqlError *err) {
    return storage_select_projection(data_dir, schema_name, table, NULL, 0, 1, out, err);
}
