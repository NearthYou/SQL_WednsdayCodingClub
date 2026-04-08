#include "execute.h"
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#define GET_PID() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#define GET_PID() getpid()
#endif

typedef struct StringList {
    char **items;
    size_t count;
} StringList;

static void string_list_init(StringList *list) {
    if (list != NULL) {
        memset(list, 0, sizeof(*list));
    }
}

static void string_list_free(StringList *list) {
    size_t i;

    if (list == NULL) {
        return;
    }

    for (i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }

    free(list->items);
    string_list_init(list);
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

static int append_unique_string(StringList *list, const char *text, SqlError *err, const char *context) {
    char **expanded;
    char *copy;
    size_t i;
    size_t new_count;
    size_t new_size;

    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], text) == 0) {
            return SQL_SUCCESS;
        }
    }

    copy = duplicate_string(text);
    if (copy == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while storing %s", context);
        return SQL_FAILURE;
    }

    if (list->count >= SIZE_MAX / sizeof(*expanded)) {
        free(copy);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while growing %s list", context);
        return SQL_FAILURE;
    }

    new_count = list->count + 1;
    new_size = new_count * sizeof(*expanded);
    expanded = (char **) realloc(list->items, new_size);
    if (expanded == NULL) {
        free(copy);
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while growing %s list", context);
        return SQL_FAILURE;
    }

    expanded[list->count] = copy;
    list->items = expanded;
    list->count++;
    return SQL_SUCCESS;
}

static int build_relative_name(char **out,
                               const char *schema,
                               const char *table,
                               const char *suffix,
                               SqlError *err) {
    size_t schema_len;
    size_t table_len;
    size_t suffix_len;
    size_t total_len;
    char *name;

    if (out == NULL || table == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "table is required");
        return SQL_FAILURE;
    }

    schema_len = (schema != NULL) ? strlen(schema) : 0;
    table_len = strlen(table);
    suffix_len = (suffix != NULL) ? strlen(suffix) : 0;
    total_len = table_len + suffix_len + 1;
    if (schema != NULL) {
        total_len += schema_len + 2;
    }

    name = (char *) malloc(total_len);
    if (name == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while building staged file name");
        return SQL_FAILURE;
    }

    if (schema != NULL) {
        snprintf(name, total_len, "%s__%s%s", schema, table, (suffix != NULL) ? suffix : "");
    } else {
        snprintf(name, total_len, "%s%s", table, (suffix != NULL) ? suffix : "");
    }

    *out = name;
    return SQL_SUCCESS;
}

static int join_dir_file(char **out, const char *dir, const char *file_name, SqlError *err) {
    size_t dir_len;
    size_t file_len;
    char *path;

    if (out == NULL || dir == NULL || file_name == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "dir and file_name are required");
        return SQL_FAILURE;
    }

    dir_len = strlen(dir);
    file_len = strlen(file_name);
    path = (char *) malloc(dir_len + 1 + file_len + 1);
    if (path == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while building file path");
        return SQL_FAILURE;
    }

    snprintf(path, dir_len + 1 + file_len + 1, "%s/%s", dir, file_name);
    *out = path;
    return SQL_SUCCESS;
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

static int copy_file_bytes(const char *source_path, const char *target_path, SqlError *err, const char *context) {
    FILE *source;
    FILE *target;
    char buffer[4096];
    size_t read_size;

    source = fopen(source_path, "rb");
    if (source == NULL) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open %s source '%s'", context, source_path);
        return SQL_FAILURE;
    }

    target = fopen(target_path, "wb");
    if (target == NULL) {
        fclose(source);
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to open %s target '%s'", context, target_path);
        return SQL_FAILURE;
    }

    while ((read_size = fread(buffer, 1u, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1u, read_size, target) != read_size) {
            fclose(source);
            fclose(target);
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write %s target '%s'", context, target_path);
            return SQL_FAILURE;
        }
    }

    if (ferror(source) || fclose(source) != 0 || fclose(target) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish %s copy '%s' -> '%s'", context, source_path, target_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int move_file_path(const char *source_path, const char *target_path, SqlError *err, const char *context) {
    if (rename(source_path, target_path) != 0) {
        sql_error_set(err,
                      SQL_ERR_IO,
                      0,
                      "Failed to move %s '%s' -> '%s'",
                      context,
                      source_path,
                      target_path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int remove_file_if_exists(const char *path, SqlError *err, const char *context) {
    if (!file_exists(path)) {
        return SQL_SUCCESS;
    }

    if (remove(path) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to remove %s '%s'", context, path);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int create_stage_dir(char **out_stage_dir, const char *data_dir, SqlError *err) {
    int attempt;

    for (attempt = 0; attempt < 128; ++attempt) {
        char *candidate;
        size_t length;

        length = strlen(data_dir) + 48;
        candidate = (char *) malloc(length);
        if (candidate == NULL) {
            sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while creating stage directory path");
            return SQL_FAILURE;
        }

        snprintf(candidate,
                 length,
                 "%s/.sql_stage_%ld_%d_%d",
                 data_dir,
                 (long) GET_PID(),
                 (int) time(NULL),
                 attempt);

        if (MAKE_DIR(candidate) == 0) {
            *out_stage_dir = candidate;
            return SQL_SUCCESS;
        }

        free(candidate);
        if (errno != EEXIST) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to create stage directory in '%s'", data_dir);
            return SQL_FAILURE;
        }
    }

    sql_error_set(err, SQL_ERR_IO, 0, "Failed to allocate a unique stage directory in '%s'", data_dir);
    return SQL_FAILURE;
}

static int create_temp_file_in_dir(FILE **out_file,
                                   char **out_path,
                                   const char *dir,
                                   const char *prefix,
                                   SqlError *err) {
    int attempt;

    for (attempt = 0; attempt < 128; ++attempt) {
        char *file_name;
        char *path;
        FILE *file;
        size_t name_len;

        name_len = strlen(prefix) + 48;
        file_name = (char *) malloc(name_len);
        if (file_name == NULL) {
            sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while creating buffered output path");
            return SQL_FAILURE;
        }

        snprintf(file_name,
                 name_len,
                 "%s_%ld_%d_%d.tmp",
                 prefix,
                 (long) GET_PID(),
                 (int) time(NULL),
                 attempt);

        if (join_dir_file(&path, dir, file_name, err) != SQL_SUCCESS) {
            free(file_name);
            return SQL_FAILURE;
        }
        free(file_name);

        file = fopen(path, "w+b");
        if (file != NULL) {
            *out_file = file;
            *out_path = path;
            return SQL_SUCCESS;
        }

        free(path);
        if (errno != EEXIST) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to create temporary file in '%s'", dir);
            return SQL_FAILURE;
        }
    }

    sql_error_set(err, SQL_ERR_IO, 0, "Failed to allocate a unique temporary file in '%s'", dir);
    return SQL_FAILURE;
}

static void cleanup_stage_dir(const char *stage_dir, StringList *managed_files) {
    size_t i;

    if (stage_dir == NULL || managed_files == NULL) {
        return;
    }

    for (i = 0; i < managed_files->count; ++i) {
        SqlError ignored_err;
        char *path;

        sql_error_clear(&ignored_err);
        if (join_dir_file(&path, stage_dir, managed_files->items[i], &ignored_err) == SQL_SUCCESS) {
            remove(path);
            free(path);
        }
    }

    if (REMOVE_DIR(stage_dir) != 0) {
        /* Stage cleanup is best-effort after execution completes. */
    }
}

static int ensure_staged_table(const char *stage_dir,
                               const char *data_dir,
                               const Statement *stmt,
                               StringList *managed_files,
                               SqlError *err) {
    char *csv_name;
    char *schema_name;
    char *stage_path;
    char *source_path;
    int result;

    csv_name = NULL;
    schema_name = NULL;
    stage_path = NULL;
    source_path = NULL;

    if (build_relative_name(&csv_name, stmt->schema, stmt->table, ".csv", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (append_unique_string(managed_files, csv_name, err, "stage file") != SQL_SUCCESS) {
        free(csv_name);
        return SQL_FAILURE;
    }

    if (build_relative_name(&schema_name, stmt->schema, stmt->table, ".schema.csv", err) != SQL_SUCCESS) {
        free(csv_name);
        return SQL_FAILURE;
    }

    if (append_unique_string(managed_files, schema_name, err, "stage file") != SQL_SUCCESS) {
        free(csv_name);
        free(schema_name);
        return SQL_FAILURE;
    }

    result = SQL_SUCCESS;
    if (join_dir_file(&stage_path, stage_dir, csv_name, err) != SQL_SUCCESS ||
        join_dir_file(&source_path, data_dir, csv_name, err) != SQL_SUCCESS) {
        result = SQL_FAILURE;
    } else if (!file_exists(stage_path) && file_exists(source_path) &&
               copy_file_bytes(source_path, stage_path, err, "stage copy") != SQL_SUCCESS) {
        result = SQL_FAILURE;
    }

    free(stage_path);
    free(source_path);
    stage_path = NULL;
    source_path = NULL;

    if (result == SQL_SUCCESS &&
        join_dir_file(&stage_path, stage_dir, schema_name, err) == SQL_SUCCESS &&
        join_dir_file(&source_path, data_dir, schema_name, err) == SQL_SUCCESS) {
        if (!file_exists(stage_path) && file_exists(source_path) &&
            copy_file_bytes(source_path, stage_path, err, "stage copy") != SQL_SUCCESS) {
            result = SQL_FAILURE;
        }
    } else {
        result = SQL_FAILURE;
    }

    free(stage_path);
    free(source_path);
    free(csv_name);
    free(schema_name);
    return result;
}

static int flush_and_copy_output(FILE *buffer, FILE *out, SqlError *err) {
    char chunk[4096];
    size_t read_size;

    if (fflush(buffer) != 0 || fseek(buffer, 0L, SEEK_SET) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to rewind buffered SQL output");
        return SQL_FAILURE;
    }

    while ((read_size = fread(chunk, 1u, sizeof(chunk), buffer)) > 0) {
        if (fwrite(chunk, 1u, read_size, out) != read_size) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write buffered SQL output");
            return SQL_FAILURE;
        }
    }

    if (ferror(buffer) || fflush(out) != 0) {
        sql_error_set(err, SQL_ERR_IO, 0, "Failed to finish buffered SQL output");
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int execute_statement_unstaged(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err) {
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
                               (const char *const *) stmt->columns,
                               stmt->column_count,
                               stmt->values,
                               stmt->value_count,
                               err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (fprintf(out,
                    "INSERT 1 INTO %s%s%s\n",
                    (stmt->schema != NULL) ? stmt->schema : "",
                    (stmt->schema != NULL) ? "." : "",
                    stmt->table) < 0) {
            sql_error_set(err, SQL_ERR_IO, 0, "Failed to write INSERT result");
            return SQL_FAILURE;
        }

        return SQL_SUCCESS;
    }

    if (stmt->type == STMT_SELECT) {
        return storage_select_projection(data_dir,
                                         stmt->schema,
                                         stmt->table,
                                         (const char *const *) stmt->columns,
                                         stmt->column_count,
                                         stmt->select_all,
                                         out,
                                         err);
    }

    sql_error_set(err, SQL_ERR_UNSUPPORTED, 0, "Unsupported statement type for execution");
    return SQL_FAILURE;
}

int execute_statement(const Statement *stmt, const char *data_dir, FILE *out, SqlError *err) {
    SqlScript script;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (stmt == NULL || data_dir == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "stmt, data_dir, and out are required");
        return SQL_FAILURE;
    }

    script.statements = (Statement *) stmt;
    script.statement_count = 1;
    return execute_script(&script, data_dir, out, err);
}

static int rollback_committed_csvs(const char *data_dir,
                                   const char *stage_dir,
                                   const StringList *touched_csvs,
                                   const StringList *backup_names,
                                   const unsigned char *had_original,
                                   size_t committed_count,
                                   SqlError *err) {
    size_t i;

    for (i = committed_count; i > 0; --i) {
        size_t index;
        SqlError local_err;
        char *target_path;
        char *backup_path;

        index = i - 1;
        sql_error_clear(&local_err);
        target_path = NULL;
        backup_path = NULL;
        if (join_dir_file(&target_path, data_dir, touched_csvs->items[index], &local_err) != SQL_SUCCESS) {
            free(target_path);
            if (err != NULL) {
                *err = local_err;
            }
            return SQL_FAILURE;
        }

        if (had_original[index]) {
            if (join_dir_file(&backup_path, stage_dir, backup_names->items[index], &local_err) != SQL_SUCCESS ||
                remove_file_if_exists(target_path, &local_err, "partially committed CSV") != SQL_SUCCESS ||
                move_file_path(backup_path, target_path, &local_err, "rollback restore") != SQL_SUCCESS) {
                free(target_path);
                free(backup_path);
                if (err != NULL) {
                    *err = local_err;
                }
                return SQL_FAILURE;
            }
        } else {
            if (remove_file_if_exists(target_path, &local_err, "newly committed CSV") != SQL_SUCCESS) {
                free(target_path);
                free(backup_path);
                if (err != NULL) {
                    *err = local_err;
                }
                return SQL_FAILURE;
            }
        }

        free(target_path);
        free(backup_path);
    }

    return SQL_SUCCESS;
}

static int commit_staged_csvs(const char *stage_dir,
                              const char *data_dir,
                              StringList *touched_csvs,
                              StringList *managed_files,
                              StringList *backup_names,
                              unsigned char **out_had_original,
                              SqlError *err) {
    unsigned char *had_original;
    size_t i;

    if (backup_names == NULL || out_had_original == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "Commit bookkeeping outputs are required");
        return SQL_FAILURE;
    }

    string_list_init(backup_names);
    *out_had_original = NULL;
    had_original = NULL;

    if (touched_csvs->count > 0) {
        had_original = (unsigned char *) calloc(touched_csvs->count, sizeof(*had_original));
        if (had_original == NULL) {
            sql_error_set(err, SQL_ERR_MEMORY, 0, "Out of memory while preparing staged commit");
            return SQL_FAILURE;
        }
    }

    for (i = 0; i < touched_csvs->count; ++i) {
        char *source_path;
        char *target_path;
        char *backup_path;
        char *temp_path;
        char backup_name[96];
        char temp_name[96];

        source_path = NULL;
        target_path = NULL;
        backup_path = NULL;
        temp_path = NULL;
        snprintf(backup_name, sizeof(backup_name), "__rollback_%u.csv", (unsigned) i);
        snprintf(temp_name, sizeof(temp_name), "__commit_%u.csv", (unsigned) i);

        if (append_unique_string(backup_names, backup_name, err, "rollback backup") != SQL_SUCCESS ||
            append_unique_string(managed_files, backup_name, err, "stage file") != SQL_SUCCESS ||
            append_unique_string(managed_files, temp_name, err, "stage file") != SQL_SUCCESS ||
            join_dir_file(&source_path, stage_dir, touched_csvs->items[i], err) != SQL_SUCCESS ||
            join_dir_file(&target_path, data_dir, touched_csvs->items[i], err) != SQL_SUCCESS ||
            join_dir_file(&backup_path, stage_dir, backup_name, err) != SQL_SUCCESS ||
            join_dir_file(&temp_path, stage_dir, temp_name, err) != SQL_SUCCESS) {
            free(source_path);
            free(target_path);
            free(backup_path);
            free(temp_path);
            free(had_original);
            string_list_free(backup_names);
            return SQL_FAILURE;
        }

        if (copy_file_bytes(source_path, temp_path, err, "commit staging") != SQL_SUCCESS) {
            free(source_path);
            free(target_path);
            free(backup_path);
            free(temp_path);
            free(had_original);
            string_list_free(backup_names);
            return SQL_FAILURE;
        }

        if (file_exists(target_path)) {
            had_original[i] = 1;
            if (move_file_path(target_path, backup_path, err, "rollback backup") != SQL_SUCCESS) {
                free(source_path);
                free(target_path);
                free(backup_path);
                free(temp_path);
                free(had_original);
                string_list_free(backup_names);
                return SQL_FAILURE;
            }
        }

        if (move_file_path(temp_path, target_path, err, "commit replace") != SQL_SUCCESS) {
            SqlError rollback_err;

            sql_error_clear(&rollback_err);
            if (had_original[i] &&
                move_file_path(backup_path, target_path, &rollback_err, "rollback restore") != SQL_SUCCESS) {
                free(source_path);
                free(target_path);
                free(backup_path);
                free(temp_path);
                free(had_original);
                string_list_free(backup_names);
                *err = rollback_err;
                return SQL_FAILURE;
            }

            if (rollback_committed_csvs(data_dir,
                                        stage_dir,
                                        touched_csvs,
                                        backup_names,
                                        had_original,
                                        i,
                                        &rollback_err) != SQL_SUCCESS) {
                free(source_path);
                free(target_path);
                free(backup_path);
                free(temp_path);
                free(had_original);
                string_list_free(backup_names);
                *err = rollback_err;
                return SQL_FAILURE;
            }

            free(source_path);
            free(target_path);
            free(backup_path);
            free(temp_path);
            free(had_original);
            string_list_free(backup_names);
            return SQL_FAILURE;
        }

        free(source_path);
        free(target_path);
        free(backup_path);
        free(temp_path);
    }

    *out_had_original = had_original;
    return SQL_SUCCESS;
}

int execute_script(const SqlScript *script, const char *data_dir, FILE *out, SqlError *err) {
    StringList touched_csvs;
    StringList managed_files;
    StringList commit_backup_names;
    FILE *buffer;
    char *buffer_path;
    char *stage_dir;
    unsigned char *commit_had_original;
    const char *exec_dir;
    int needs_staging;
    size_t i;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (script == NULL || data_dir == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "script, data_dir, and out are required");
        return SQL_FAILURE;
    }

    if (script->statements == NULL || script->statement_count == 0) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "SQL script must contain at least one statement");
        return SQL_FAILURE;
    }

    needs_staging = 0;
    for (i = 0; i < script->statement_count; ++i) {
        if (script->statements[i].type == STMT_INSERT) {
            needs_staging = 1;
            break;
        }
    }

    string_list_init(&touched_csvs);
    string_list_init(&managed_files);
    string_list_init(&commit_backup_names);
    buffer = NULL;
    buffer_path = NULL;
    stage_dir = NULL;
    commit_had_original = NULL;
    exec_dir = data_dir;

    if (needs_staging) {
        if (create_stage_dir(&stage_dir, data_dir, err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }
        exec_dir = stage_dir;
    }

    if (create_temp_file_in_dir(&buffer,
                                &buffer_path,
                                exec_dir,
                                "__sql_output",
                                err) != SQL_SUCCESS) {
        cleanup_stage_dir(stage_dir, &managed_files);
        free(stage_dir);
        string_list_free(&managed_files);
        string_list_free(&touched_csvs);
        return SQL_FAILURE;
    }

    for (i = 0; i < script->statement_count; ++i) {
        const Statement *stmt;

        stmt = &script->statements[i];
        if (needs_staging && ensure_staged_table(stage_dir, data_dir, stmt, &managed_files, err) != SQL_SUCCESS) {
            fclose(buffer);
            remove(buffer_path);
            free(buffer_path);
            cleanup_stage_dir(stage_dir, &managed_files);
            free(stage_dir);
            string_list_free(&managed_files);
            string_list_free(&touched_csvs);
            return SQL_FAILURE;
        }

        if (execute_statement_unstaged(stmt, exec_dir, buffer, err) != SQL_SUCCESS) {
            fclose(buffer);
            remove(buffer_path);
            free(buffer_path);
            cleanup_stage_dir(stage_dir, &managed_files);
            free(stage_dir);
            string_list_free(&managed_files);
            string_list_free(&touched_csvs);
            return SQL_FAILURE;
        }

        if (needs_staging && stmt->type == STMT_INSERT) {
            char *csv_name;

            csv_name = NULL;
            if (build_relative_name(&csv_name, stmt->schema, stmt->table, ".csv", err) != SQL_SUCCESS ||
                append_unique_string(&touched_csvs, csv_name, err, "touched CSV") != SQL_SUCCESS) {
                free(csv_name);
                fclose(buffer);
                remove(buffer_path);
                free(buffer_path);
                cleanup_stage_dir(stage_dir, &managed_files);
                free(stage_dir);
                string_list_free(&managed_files);
                string_list_free(&touched_csvs);
                return SQL_FAILURE;
            }

            free(csv_name);
        }
    }

    if (needs_staging &&
        commit_staged_csvs(stage_dir,
                           data_dir,
                           &touched_csvs,
                           &managed_files,
                           &commit_backup_names,
                           &commit_had_original,
                           err) != SQL_SUCCESS) {
        fclose(buffer);
        remove(buffer_path);
        free(buffer_path);
        cleanup_stage_dir(stage_dir, &managed_files);
        free(stage_dir);
        string_list_free(&commit_backup_names);
        free(commit_had_original);
        string_list_free(&managed_files);
        string_list_free(&touched_csvs);
        return SQL_FAILURE;
    }

    if (flush_and_copy_output(buffer, out, err) != SQL_SUCCESS) {
        if (needs_staging) {
            SqlError rollback_err;

            sql_error_clear(&rollback_err);
            if (rollback_committed_csvs(data_dir,
                                        stage_dir,
                                        &touched_csvs,
                                        &commit_backup_names,
                                        commit_had_original,
                                        touched_csvs.count,
                                        &rollback_err) == SQL_SUCCESS) {
                /* Keep the original output error when data rollback succeeds. */
            } else {
                *err = rollback_err;
            }
        }
        fclose(buffer);
        remove(buffer_path);
        free(buffer_path);
        cleanup_stage_dir(stage_dir, &managed_files);
        free(stage_dir);
        string_list_free(&commit_backup_names);
        free(commit_had_original);
        string_list_free(&managed_files);
        string_list_free(&touched_csvs);
        return SQL_FAILURE;
    }

    fclose(buffer);
    remove(buffer_path);
    free(buffer_path);
    cleanup_stage_dir(stage_dir, &managed_files);
    free(stage_dir);
    string_list_free(&commit_backup_names);
    free(commit_had_original);
    string_list_free(&managed_files);
    string_list_free(&touched_csvs);
    return SQL_SUCCESS;
}
