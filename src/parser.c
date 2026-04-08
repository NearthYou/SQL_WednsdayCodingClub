#include "parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 수동 스캐너의 현재 위치를 추적하는 상태 구조다. */
typedef struct Parser {
    const char *input;
    size_t length;
    size_t pos;
} Parser;

static int ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }

    return ch;
}

static int is_identifier_start(char ch) {
    return isalpha((unsigned char) ch) || ch == '_';
}

static int is_identifier_char(char ch) {
    return isalnum((unsigned char) ch) || ch == '_';
}

static char current_char(const Parser *parser) {
    if (parser->pos >= parser->length) {
        return '\0';
    }

    return parser->input[parser->pos];
}

static char peek_char(const Parser *parser, size_t offset) {
    if (parser->pos + offset >= parser->length) {
        return '\0';
    }

    return parser->input[parser->pos + offset];
}

static void skip_whitespace(Parser *parser) {
    while (parser->pos < parser->length &&
           isspace((unsigned char) parser->input[parser->pos])) {
        parser->pos++;
    }
}

static void skip_utf8_bom(Parser *parser) {
    if (parser->length >= 3 &&
        (unsigned char) parser->input[0] == 0xEF &&
        (unsigned char) parser->input[1] == 0xBB &&
        (unsigned char) parser->input[2] == 0xBF) {
        parser->pos = 3;
    }
}

static char *copy_substring(const char *start, size_t length) {
    char *buffer;

    buffer = (char *) malloc(length + 1);
    if (buffer == NULL) {
        return NULL;
    }

    if (length > 0) {
        memcpy(buffer, start, length);
    }
    buffer[length] = '\0';
    return buffer;
}

static int append_char_to_buffer(char **buffer,
                                 size_t *length,
                                 size_t *capacity,
                                 char ch,
                                 SqlError *err,
                                 size_t position,
                                 const char *context) {
    char *expanded;
    size_t new_capacity;

    if (*length + 1 >= *capacity) {
        if (*capacity > SIZE_MAX / 2) {
            sql_error_set(err, SQL_ERR_MEMORY, position, "Out of memory while reading %s", context);
            return SQL_FAILURE;
        }

        new_capacity = (*capacity == 0) ? 16 : (*capacity * 2);
        if (new_capacity <= *length + 1) {
            if (*length > SIZE_MAX - 2) {
                sql_error_set(err, SQL_ERR_MEMORY, position, "Out of memory while reading %s", context);
                return SQL_FAILURE;
            }

            new_capacity = *length + 2;
        }

        expanded = (char *) realloc(*buffer, new_capacity);
        if (expanded == NULL) {
            sql_error_set(err, SQL_ERR_MEMORY, position, "Out of memory while reading %s", context);
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

/* 예약어 뒤에 식별자가 바로 붙는 경우를 막아 정확한 키워드만 소비한다. */
static int match_keyword(Parser *parser, const char *keyword) {
    size_t start;
    size_t i;

    start = parser->pos;
    for (i = 0; keyword[i] != '\0'; ++i) {
        if (start + i >= parser->length) {
            return 0;
        }

        if (ascii_tolower((unsigned char) parser->input[start + i]) !=
            ascii_tolower((unsigned char) keyword[i])) {
            return 0;
        }
    }

    if (start + i < parser->length &&
        is_identifier_char(parser->input[start + i])) {
        return 0;
    }

    parser->pos = start + i;
    return 1;
}

static int expect_keyword(Parser *parser, const char *keyword, SqlError *err) {
    size_t position;

    skip_whitespace(parser);
    position = parser->pos;

    if (!match_keyword(parser, keyword)) {
        sql_error_set(err, SQL_ERR_PARSE, position, "Expected keyword %s", keyword);
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int parse_identifier(Parser *parser, char **out, SqlError *err) {
    size_t start;
    size_t length;
    char *name;

    skip_whitespace(parser);
    start = parser->pos;

    if (!is_identifier_start(current_char(parser))) {
        sql_error_set(err, SQL_ERR_LEX, parser->pos, "Expected identifier");
        return SQL_FAILURE;
    }

    parser->pos++;
    while (is_identifier_char(current_char(parser))) {
        parser->pos++;
    }

    length = parser->pos - start;
    name = copy_substring(parser->input + start, length);
    if (name == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, parser->pos, "Out of memory while reading identifier");
        return SQL_FAILURE;
    }

    *out = name;
    return SQL_SUCCESS;
}

static int append_name(char ***names, size_t *count, char *name, SqlError *err, size_t position) {
    char **expanded;
    size_t new_count;
    size_t new_size;
    size_t i;

    for (i = 0; i < *count; ++i) {
        if (strcmp((*names)[i], name) == 0) {
            free(name);
            sql_error_set(err, SQL_ERR_PARSE, position, "Duplicate identifier in column list");
            return SQL_FAILURE;
        }
    }

    if (*count >= SIZE_MAX / sizeof(*expanded)) {
        free(name);
        sql_error_set(err, SQL_ERR_MEMORY, position, "Identifier list is too large to allocate safely");
        return SQL_FAILURE;
    }

    new_count = *count + 1;
    new_size = new_count * sizeof(*expanded);
    expanded = (char **) realloc(*names, new_size);
    if (expanded == NULL) {
        free(name);
        sql_error_set(err, SQL_ERR_MEMORY, position, "Out of memory while growing identifier list");
        return SQL_FAILURE;
    }

    expanded[*count] = name;
    *names = expanded;
    *count = new_count;
    return SQL_SUCCESS;
}

static int append_statement(SqlScript *script, Statement *stmt, SqlError *err, size_t position) {
    Statement *expanded;
    size_t new_count;
    size_t new_size;

    if (script->statement_count >= SIZE_MAX / sizeof(*expanded)) {
        sql_error_set(err, SQL_ERR_MEMORY, position, "SQL script is too large to allocate safely");
        return SQL_FAILURE;
    }

    new_count = script->statement_count + 1;
    new_size = new_count * sizeof(*expanded);
    expanded = (Statement *) realloc(script->statements, new_size);
    if (expanded == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, position, "Out of memory while growing statement list");
        return SQL_FAILURE;
    }

    expanded[script->statement_count] = *stmt;
    script->statements = expanded;
    script->statement_count = new_count;
    statement_init(stmt);
    return SQL_SUCCESS;
}

static int parse_qualified_name(Parser *parser, Statement *stmt, SqlError *err) {
    char *first;
    char *second;

    first = NULL;
    second = NULL;

    if (parse_identifier(parser, &first, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    skip_whitespace(parser);
    if (current_char(parser) == '.') {
        parser->pos++;
        if (parse_identifier(parser, &second, err) != SQL_SUCCESS) {
            free(first);
            return SQL_FAILURE;
        }

        stmt->schema = first;
        stmt->table = second;
        return SQL_SUCCESS;
    }

    stmt->table = first;
    return SQL_SUCCESS;
}

static int consume_char(Parser *parser, char expected, SqlError *err) {
    skip_whitespace(parser);
    if (current_char(parser) != expected) {
        sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Expected '%c'", expected);
        return SQL_FAILURE;
    }

    parser->pos++;
    return SQL_SUCCESS;
}

static int parse_integer_value(Parser *parser, SqlValue *value, SqlError *err) {
    size_t start;
    char *buffer;
    char *endptr;
    long long parsed;

    start = parser->pos;
    if (current_char(parser) == '+' || current_char(parser) == '-') {
        parser->pos++;
    }

    if (!isdigit((unsigned char) current_char(parser))) {
        sql_error_set(err, SQL_ERR_LEX, start, "Expected integer literal");
        return SQL_FAILURE;
    }

    while (isdigit((unsigned char) current_char(parser))) {
        parser->pos++;
    }

    buffer = copy_substring(parser->input + start, parser->pos - start);
    if (buffer == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, parser->pos, "Out of memory while reading integer");
        return SQL_FAILURE;
    }

    errno = 0;
    parsed = strtoll(buffer, &endptr, 10);
    if (errno == ERANGE || endptr == buffer || *endptr != '\0') {
        free(buffer);
        sql_error_set(err, SQL_ERR_LEX, start, "Invalid integer literal");
        return SQL_FAILURE;
    }

    free(buffer);
    value->type = SQL_VALUE_INT;
    value->as.int_value = parsed;
    return SQL_SUCCESS;
}

/* SQL 표준처럼 ''를 문자열 내부 작은따옴표 escape로 허용한다. */
static int parse_string_value(Parser *parser, SqlValue *value, SqlError *err) {
    char *buffer;
    size_t length;
    size_t capacity;

    if (current_char(parser) != '\'') {
        sql_error_set(err, SQL_ERR_LEX, parser->pos, "Expected string literal");
        return SQL_FAILURE;
    }

    parser->pos++;
    buffer = NULL;
    length = 0;
    capacity = 0;

    while (parser->pos < parser->length) {
        char ch;

        ch = parser->input[parser->pos];
        if (ch == '\'') {
            if (peek_char(parser, 1) == '\'') {
                if (append_char_to_buffer(&buffer,
                                          &length,
                                          &capacity,
                                          '\'',
                                          err,
                                          parser->pos,
                                          "string") != SQL_SUCCESS) {
                    free(buffer);
                    return SQL_FAILURE;
                }
                parser->pos += 2;
                continue;
            }

            parser->pos++;
            if (buffer == NULL) {
                buffer = copy_substring("", 0);
                if (buffer == NULL) {
                    sql_error_set(err, SQL_ERR_MEMORY, parser->pos, "Out of memory while reading string");
                    return SQL_FAILURE;
                }
            }
            value->type = SQL_VALUE_STRING;
            value->as.string_value = buffer;
            return SQL_SUCCESS;
        }

        if (append_char_to_buffer(&buffer,
                                  &length,
                                  &capacity,
                                  ch,
                                  err,
                                  parser->pos,
                                  "string") != SQL_SUCCESS) {
            free(buffer);
            return SQL_FAILURE;
        }

        parser->pos++;
    }

    free(buffer);
    sql_error_set(err, SQL_ERR_LEX, parser->pos, "Unterminated string literal");
    return SQL_FAILURE;
}

static void free_value(SqlValue *value) {
    if (value == NULL) {
        return;
    }

    if (value->type == SQL_VALUE_STRING) {
        free(value->as.string_value);
        value->as.string_value = NULL;
    }
}

static int append_value(Statement *stmt, const SqlValue *value, SqlError *err, size_t position) {
    size_t new_count;
    size_t new_size;
    SqlValue *expanded;

    if (stmt->value_count >= SIZE_MAX / sizeof(*stmt->values)) {
        sql_error_set(err, SQL_ERR_MEMORY, position, "VALUES list is too large to allocate safely");
        return SQL_FAILURE;
    }

    new_count = stmt->value_count + 1;
    new_size = new_count * sizeof(*stmt->values);
    expanded = (SqlValue *) realloc(stmt->values, new_size);
    if (expanded == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, position, "Out of memory while growing VALUES list");
        return SQL_FAILURE;
    }

    stmt->values = expanded;
    stmt->values[stmt->value_count] = *value;
    stmt->value_count++;
    return SQL_SUCCESS;
}

/* 값 종류를 보고 정수 파서와 문자열 파서 중 하나로 분기한다. */
static int parse_value(Parser *parser, SqlValue *out, SqlError *err) {
    skip_whitespace(parser);
    memset(out, 0, sizeof(*out));

    if (current_char(parser) == '\'') {
        return parse_string_value(parser, out, err);
    }

    if (current_char(parser) == '+' || current_char(parser) == '-' ||
        isdigit((unsigned char) current_char(parser))) {
        return parse_integer_value(parser, out, err);
    }

    sql_error_set(err, SQL_ERR_LEX, parser->pos, "Expected integer or string literal");
    return SQL_FAILURE;
}

static int parse_parenthesized_identifier_list(Parser *parser,
                                               char ***names,
                                               size_t *count,
                                               SqlError *err) {
    if (consume_char(parser, '(', err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    skip_whitespace(parser);
    if (current_char(parser) == ')') {
        sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Column list must contain at least one identifier");
        return SQL_FAILURE;
    }

    while (1) {
        char *name;

        name = NULL;
        if (parse_identifier(parser, &name, err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (append_name(names, count, name, err, parser->pos) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        skip_whitespace(parser);
        if (current_char(parser) == ')') {
            parser->pos++;
            return SQL_SUCCESS;
        }

        if (current_char(parser) != ',') {
            sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Expected ',' or ')' in column list");
            return SQL_FAILURE;
        }

        parser->pos++;
        skip_whitespace(parser);
        if (current_char(parser) == ')') {
            sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Trailing comma is not allowed in column list");
            return SQL_FAILURE;
        }
    }
}

static int parse_identifier_sequence(Parser *parser,
                                     char ***names,
                                     size_t *count,
                                     SqlError *err) {
    while (1) {
        char *name;

        name = NULL;
        if (parse_identifier(parser, &name, err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (append_name(names, count, name, err, parser->pos) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        skip_whitespace(parser);
        if (current_char(parser) != ',') {
            return SQL_SUCCESS;
        }

        parser->pos++;
        skip_whitespace(parser);
        if (current_char(parser) == '\0' || current_char(parser) == ';') {
            sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Trailing comma is not allowed in identifier list");
            return SQL_FAILURE;
        }
    }
}

/* VALUES (...) 내부를 순서대로 읽어 가변 길이 배열로 축적한다. */
static int parse_values_list(Parser *parser, Statement *stmt, SqlError *err) {
    if (consume_char(parser, '(', err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    skip_whitespace(parser);
    if (current_char(parser) == ')') {
        sql_error_set(err,
                      SQL_ERR_UNSUPPORTED,
                      parser->pos,
                      "VALUES list must contain at least one literal");
        return SQL_FAILURE;
    }

    while (1) {
        SqlValue value;

        if (parse_value(parser, &value, err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (append_value(stmt, &value, err, parser->pos) != SQL_SUCCESS) {
            free_value(&value);
            return SQL_FAILURE;
        }

        skip_whitespace(parser);
        if (current_char(parser) == ')') {
            parser->pos++;
            return SQL_SUCCESS;
        }

        if (current_char(parser) == ',') {
            parser->pos++;
            skip_whitespace(parser);
            if (current_char(parser) == ')') {
                sql_error_set(err,
                              SQL_ERR_PARSE,
                              parser->pos,
                              "Trailing comma is not allowed in VALUES list");
                return SQL_FAILURE;
            }

            continue;
        }

        sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Expected ',' or ')' in VALUES list");
        return SQL_FAILURE;
    }
}

/* INSERT 문에서 optional column list와 VALUES 절을 읽는다. */
static int parse_insert(Parser *parser, Statement *stmt, SqlError *err) {
    stmt->type = STMT_INSERT;

    if (expect_keyword(parser, "INTO", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (parse_qualified_name(parser, stmt, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    skip_whitespace(parser);
    if (current_char(parser) == '(') {
        if (parse_parenthesized_identifier_list(parser,
                                                &stmt->columns,
                                                &stmt->column_count,
                                                err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }
    }

    if (expect_keyword(parser, "VALUES", err) != SQL_SUCCESS) {
        sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Expected VALUES after INSERT target");
        return SQL_FAILURE;
    }

    if (parse_values_list(parser, stmt, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (stmt->column_count > 0 && stmt->column_count != stmt->value_count) {
        sql_error_set(err,
                      SQL_ERR_PARSE,
                      parser->pos,
                      "Column count and VALUES count must match");
        return SQL_FAILURE;
    }

    return SQL_SUCCESS;
}

static int parse_select_columns(Parser *parser, Statement *stmt, SqlError *err) {
    skip_whitespace(parser);
    if (current_char(parser) == '*') {
        stmt->select_all = 1;
        parser->pos++;
        return SQL_SUCCESS;
    }

    stmt->select_all = 0;
    return parse_identifier_sequence(parser, &stmt->columns, &stmt->column_count, err);
}

/* SELECT는 * 또는 명시적 컬럼 목록을 읽고 FROM 대상을 파싱한다. */
static int parse_select(Parser *parser, Statement *stmt, SqlError *err) {
    stmt->type = STMT_SELECT;

    if (parse_select_columns(parser, stmt, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (expect_keyword(parser, "FROM", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    return parse_qualified_name(parser, stmt, err);
}

static int parse_statement_core(Parser *parser, Statement *stmt, SqlError *err) {
    skip_whitespace(parser);
    if (match_keyword(parser, "INSERT")) {
        return parse_insert(parser, stmt, err);
    }

    if (match_keyword(parser, "SELECT")) {
        return parse_select(parser, stmt, err);
    }

    sql_error_set(err,
                  SQL_ERR_UNSUPPORTED,
                  parser->pos,
                  "Only INSERT and SELECT statements are supported");
    return SQL_FAILURE;
}

/* 여러 SQL 문장을 순서대로 파싱해 script AST에 담는다. */
int parse_sql_script(const char *sql, SqlScript *out, SqlError *err) {
    Parser parser;
    SqlScript script;
    Statement stmt;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (sql == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "SQL input and output script are required");
        return SQL_FAILURE;
    }

    parser.input = sql;
    parser.length = strlen(sql);
    parser.pos = 0;
    skip_utf8_bom(&parser);

    sql_script_init(&script);

    while (1) {
        skip_whitespace(&parser);
        if (parser.pos >= parser.length) {
            break;
        }

        statement_init(&stmt);
        if (parse_statement_core(&parser, &stmt, err) != SQL_SUCCESS) {
            statement_free(&stmt);
            sql_script_free(&script);
            return SQL_FAILURE;
        }

        skip_whitespace(&parser);
        if (current_char(&parser) != ';') {
            statement_free(&stmt);
            sql_script_free(&script);
            sql_error_set(err, SQL_ERR_PARSE, parser.pos, "Expected ';' at end of statement");
            return SQL_FAILURE;
        }
        parser.pos++;

        if (append_statement(&script, &stmt, err, parser.pos) != SQL_SUCCESS) {
            statement_free(&stmt);
            sql_script_free(&script);
            return SQL_FAILURE;
        }
    }

    if (script.statement_count == 0) {
        sql_error_set(err, SQL_ERR_PARSE, parser.pos, "SQL input is empty");
        return SQL_FAILURE;
    }

    *out = script;
    return SQL_SUCCESS;
}

/* 단일 Statement API는 호환을 위해 유지하되, 여러 문장은 실패시킨다. */
int parse_sql(const char *sql, Statement *out, SqlError *err) {
    SqlScript script;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (sql == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "SQL input and output statement are required");
        return SQL_FAILURE;
    }

    sql_script_init(&script);
    if (parse_sql_script(sql, &script, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (script.statement_count != 1) {
        sql_script_free(&script);
        sql_error_set(err, SQL_ERR_UNSUPPORTED, 0, "Only one SQL statement per file is allowed in parse_sql");
        return SQL_FAILURE;
    }

    *out = script.statements[0];
    free(script.statements);
    return SQL_SUCCESS;
}
