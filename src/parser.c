#include "parse.h"

#include <ctype.h>
#include <errno.h>
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

static void skip_whitespace(Parser *parser) {
    while (parser->pos < parser->length &&
           isspace((unsigned char) parser->input[parser->pos])) {
        parser->pos++;
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

/* MVP에서는 escape 없이 작은따옴표로 감싼 문자열만 허용한다. */
static int parse_string_value(Parser *parser, SqlValue *value, SqlError *err) {
    size_t start;
    size_t length;
    char *buffer;

    if (current_char(parser) != '\'') {
        sql_error_set(err, SQL_ERR_LEX, parser->pos, "Expected string literal");
        return SQL_FAILURE;
    }

    parser->pos++;
    start = parser->pos;

    while (parser->pos < parser->length && parser->input[parser->pos] != '\'') {
        parser->pos++;
    }

    if (parser->pos >= parser->length) {
        sql_error_set(err, SQL_ERR_LEX, start, "Unterminated string literal");
        return SQL_FAILURE;
    }

    length = parser->pos - start;
    buffer = copy_substring(parser->input + start, length);
    if (buffer == NULL) {
        sql_error_set(err, SQL_ERR_MEMORY, parser->pos, "Out of memory while reading string");
        return SQL_FAILURE;
    }

    parser->pos++;
    value->type = SQL_VALUE_STRING;
    value->as.string_value = buffer;
    return SQL_SUCCESS;
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
    SqlValue *expanded;

    expanded = (SqlValue *) realloc(stmt->values, (stmt->value_count + 1) * sizeof(*stmt->values));
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

/* VALUES (...) 내부를 순서대로 읽어 가변 길이 배열로 축적한다. */
static int parse_values_list(Parser *parser, Statement *stmt, SqlError *err) {
    int saw_value;

    if (consume_char(parser, '(', err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    saw_value = 0;
    while (1) {
        SqlValue value;

        skip_whitespace(parser);
        if (current_char(parser) == ')') {
            if (!saw_value) {
                sql_error_set(err,
                              SQL_ERR_UNSUPPORTED,
                              parser->pos,
                              "VALUES list must contain at least one literal");
                return SQL_FAILURE;
            }

            parser->pos++;
            return SQL_SUCCESS;
        }

        if (parse_value(parser, &value, err) != SQL_SUCCESS) {
            return SQL_FAILURE;
        }

        if (append_value(stmt, &value, err, parser->pos) != SQL_SUCCESS) {
            free_value(&value);
            return SQL_FAILURE;
        }

        saw_value = 1;
        skip_whitespace(parser);
        if (current_char(parser) == ',') {
            parser->pos++;
            continue;
        }

        if (current_char(parser) == ')') {
            parser->pos++;
            return SQL_SUCCESS;
        }

        sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Expected ',' or ')' in VALUES list");
        return SQL_FAILURE;
    }
}

/* INSERT 문의 나머지 문법을 읽고 Statement를 채운다. */
static int parse_insert(Parser *parser, Statement *stmt, SqlError *err) {
    stmt->type = STMT_INSERT;

    if (expect_keyword(parser, "INTO", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (parse_qualified_name(parser, stmt, err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    if (expect_keyword(parser, "VALUES", err) != SQL_SUCCESS) {
        sql_error_set(err, SQL_ERR_PARSE, parser->pos, "Expected VALUES after INSERT target");
        return SQL_FAILURE;
    }

    return parse_values_list(parser, stmt, err);
}

/* SELECT는 Phase 1에서 SELECT * FROM ... 형태만 허용한다. */
static int parse_select(Parser *parser, Statement *stmt, SqlError *err) {
    stmt->type = STMT_SELECT;

    skip_whitespace(parser);
    if (current_char(parser) != '*') {
        sql_error_set(err, SQL_ERR_UNSUPPORTED, parser->pos, "Only SELECT * is supported");
        return SQL_FAILURE;
    }
    parser->pos++;

    if (expect_keyword(parser, "FROM", err) != SQL_SUCCESS) {
        return SQL_FAILURE;
    }

    return parse_qualified_name(parser, stmt, err);
}

/* Phase 1 파서의 진입점이다. 성공 시에만 완성된 AST를 호출자에게 넘긴다. */
int parse_sql(const char *sql, Statement *out, SqlError *err) {
    Parser parser;
    Statement stmt;
    size_t first_non_space;

    if (err == NULL) {
        return SQL_FAILURE;
    }

    sql_error_clear(err);
    if (sql == NULL || out == NULL) {
        sql_error_set(err, SQL_ERR_ARGUMENT, 0, "SQL input and output statement are required");
        return SQL_FAILURE;
    }

    statement_init(&stmt);

    parser.input = sql;
    parser.length = strlen(sql);
    parser.pos = 0;

    first_non_space = 0;
    while (first_non_space < parser.length &&
           isspace((unsigned char) sql[first_non_space])) {
        first_non_space++;
    }

    if (first_non_space == parser.length) {
        sql_error_set(err, SQL_ERR_PARSE, 0, "SQL input is empty");
        return SQL_FAILURE;
    }

    skip_whitespace(&parser);
    if (match_keyword(&parser, "INSERT")) {
        if (parse_insert(&parser, &stmt, err) != SQL_SUCCESS) {
            statement_free(&stmt);
            return SQL_FAILURE;
        }
    } else if (match_keyword(&parser, "SELECT")) {
        if (parse_select(&parser, &stmt, err) != SQL_SUCCESS) {
            statement_free(&stmt);
            return SQL_FAILURE;
        }
    } else {
        sql_error_set(err,
                      SQL_ERR_UNSUPPORTED,
                      parser.pos,
                      "Only INSERT and SELECT statements are supported");
        return SQL_FAILURE;
    }

    /* 세미콜론 뒤에 남는 문자가 있으면 다중 문장으로 간주해 실패시킨다. */
    skip_whitespace(&parser);
    if (current_char(&parser) != ';') {
        statement_free(&stmt);
        sql_error_set(err, SQL_ERR_PARSE, parser.pos, "Expected ';' at end of statement");
        return SQL_FAILURE;
    }
    parser.pos++;

    skip_whitespace(&parser);
    if (parser.pos != parser.length) {
        statement_free(&stmt);
        sql_error_set(err,
                      SQL_ERR_UNSUPPORTED,
                      parser.pos,
                      "Only one SQL statement per file is allowed");
        return SQL_FAILURE;
    }

    *out = stmt;
    return SQL_SUCCESS;
}
