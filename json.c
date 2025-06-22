#include "json.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct JSONNumber JSONNumber;
typedef struct JSONString JSONString;

/*
 * JSONValue is defined to just hold the type as it is
 * not supposed to be constructed on its own, but dynamically allocated
 * as different objects as identified by the tag, which
 * it can then be cast to at runtime.
 */
struct JSONValue {
	JSONType type;
};

struct JSONNumber {
	JSONValue value;
	double number;
};

struct JSONString {
	JSONValue value;
	char * string;
};

struct JSONArray {
	JSONValue value;
	JSONValue ** values;
	size_t size;
};

struct JSONObject {
	JSONValue value;
	char ** strings;
	JSONValue ** values;
	size_t count;
};

static JSONValue json_null = { JSON_NULL };
static JSONValue json_true = { JSON_BOOL };
static JSONValue json_false = { JSON_BOOL };

typedef enum {
	TT_NULL,
	TT_TRUE,
	TT_FALSE,
	TT_LBRACE,
	TT_RBRACE,
	TT_LBRACKET,
	TT_RBRACKET,
	TT_COMMA,
	TT_COLON,
	TT_NUMBER,
	TT_STRING,
	TT_EOF,
	TT_ERROR
} TokenType;

typedef struct {
	TokenType type;
	union {
		double number;
		char * string;
	} as;
} Token;

static Token error_token(void) {
	Token token;
	token.type = TT_ERROR;
	return token;
}

#define ERROR_TOKEN error_token()

typedef struct {
	const char * src;
} Lexer;

typedef struct {
	Lexer lexer;
	JSONAllocator allocator;
} Ctx;

static void * ctx_reallocate(Ctx * ctx, void * old_alloc, size_t old_size, size_t new_size) {
	return ctx->allocator.callback(ctx->allocator.ctx, old_alloc, old_size, new_size);
}

static void allocator_free(void * old_alloc, size_t old_size, JSONAllocator allocator) {
	allocator.callback(allocator.ctx, old_alloc, old_size, 0);
}

static void allocator_free_array(void * old_alloc, size_t old_size, size_t element_size, JSONAllocator allocator) {
	allocator.callback(allocator.ctx, old_alloc, old_size * element_size, 0);
}

static void * ctx_grow_array(Ctx * ctx, void * old_alloc, size_t old_size, size_t new_size, size_t element_size) {
	return ctx_reallocate(ctx, old_alloc, old_size * element_size, new_size * element_size);
}

static void ctx_free_array(Ctx * ctx, void * old_alloc, size_t old_size, size_t element_size) {
	allocator_free_array(old_alloc, old_size, element_size, ctx->allocator);
}

static Lexer lexer_new(const char * src) {
	Lexer lexer;
	lexer.src = src;
	return lexer;
}

static int c_is_digit(char c) {
	return '0' <= c && c <= '9';
}

static int c_is_upper(char c) {
	return 'A' <= c && c <= 'Z';
}

static int c_is_lower(char c) {
	return 'a' <= c && c <= 'z';
}

static int c_is_alpha(char c) {
	return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

static int lexer_eof(const Lexer * lexer) {
	return *lexer->src == '\0';
}

static char lexer_next(Lexer * lexer) {
	return lexer_eof(lexer) ? '\0' : *(lexer->src++);
}

static char lexer_peek(Lexer * lexer) {
	return *lexer->src;
}

static void lexer_advance(Lexer * lexer) {
	if (!lexer_eof(lexer)) {
		++lexer->src;
	}
}

static int try_append_unverified_codepoint(Ctx * ctx, char ** str, size_t * size, uint32_t codepoint) {
	char * new_str;
	char * start;
	if (codepoint < 0x0020 || codepoint > 0x10FFFF) {
		return 0;
	}
	if (codepoint < 0x80) {
		new_str = ctx_reallocate(ctx, *str, *size, *size + 1);
		if (!new_str) {
			return 0;
		}
		new_str[*size] = codepoint;
		*str = new_str;
		++*size;
		return 1;
	}
	if (codepoint < 0x800) {
		new_str = ctx_reallocate(ctx, *str, *size, *size + 2);
		if (!new_str) {
			return 0;
		}
		start = new_str + *size;
		start[0] = 0xC0 | ((codepoint >> 6) & 0x1F);
		start[1] = 0x80 | (codepoint & 0x3F);
		*str = new_str;
		*size += 2;
		return 1;
	}
	if (codepoint < 0x10000) {
		if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
			return 0;
		}
		new_str = ctx_reallocate(ctx, *str, *size, *size + 3);
		if (!new_str) {
			return 0;
		}
		start = new_str + *size;
		start[0] = 0xE0 | ((codepoint >> 12) & 0x0F);
		start[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		start[2] = 0x80 | (codepoint & 0x3F);
		*str = new_str;
		*size += 3;
		return 1;
	}
	/* codepoint >= 0x10000 */
	new_str = ctx_reallocate(ctx, *str, *size, *size + 4);
	if (!new_str) {
		return 0;
	}
	start = new_str + *size;
	start[0] = 0xF0 | ((codepoint >> 18) & 0x07);
	start[1] = 0x80 | ((codepoint >> 12) & 0x3F);
	start[2] = 0x80 | ((codepoint >> 6) & 0x3F);
	start[3] = 0x80 | (codepoint & 0x3F);
	*str = new_str;
	*size += 4;
	return 1;
}

static Token lex_rest_of_string(Ctx * ctx) {
	char * str = NULL;
	char * new_str;
	size_t size = 0;
	char c;
	Token token;
	while ((c = lexer_next(&ctx->lexer)) != '"') {
		if (c == '\\') {
			switch (lexer_next(&ctx->lexer)) {
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case '"':
				c = '"';
				break;
			case '\\':
				c = '\\';
				break;
			case '/':
				c = '/';
				break;
			case 'u': {
				uint32_t codepoint = 0;
				size_t i;
				for (i = 0; i < 4; i++) {
					char c = lexer_next(&ctx->lexer);
					codepoint <<= 4;
					if (c_is_digit(c)) {
						codepoint |= c - '0';
					} else if ('a' <= c && c <= 'f') {
						codepoint |= c - 'a' + 10;
					} else if ('A' <= c && c <= 'F') {
						codepoint |= c - 'A' + 10;
					} else {
						goto error;
					}
				}
				if (!try_append_unverified_codepoint(ctx, &str, &size, codepoint)) {
					goto error;
				}
				goto outer;
			}
			default:
					  goto error;
			}
		}
		new_str = ctx_reallocate(ctx, str, size, size + 1);
		if (!new_str) {
			goto error;
		}
		str = new_str;
		str[size] = c;
		++size;
outer:;
	}
	new_str = ctx_reallocate(ctx, str, size, size + 1);
	if (!new_str) {
		goto error;
	}
	str = new_str;
	str[size] = '\0';
	token.type = TT_STRING;
	token.as.string = str;
	return token;
error:
	allocator_free(str, size, ctx->allocator);
	return ERROR_TOKEN;
}

static Token lex_number(Ctx * ctx) {
	char * next;
	double value;
	Token token;
	errno = 0;
	value = strtod(ctx->lexer.src, &next);
	if (errno) {
		return ERROR_TOKEN;
	}
	ctx->lexer.src = next;
	token.type = TT_NUMBER;
	token.as.number = value;
	return token;
}

static Token token_new(TokenType type) {
	Token token;
	token.type = type;
	return token;
}

static int starts_with(const char * prefix, const char * str) {
	char c;
	while ((c = *prefix) == *str) {
		if (c == '\0') {
			return 1;
		}
		++prefix;
		++str;
	}
	return *prefix == '\0';
}

static Token lex_identifier(Ctx * ctx) {
	if (starts_with("null", ctx->lexer.src)) {
		ctx->lexer.src += 4;
		return token_new(TT_NULL);
	}
	if (starts_with("true", ctx->lexer.src)) {
		ctx->lexer.src += 4;
		return token_new(TT_TRUE);
	}
	if (starts_with("false", ctx->lexer.src)) {
		ctx->lexer.src += 5;
		return token_new(TT_FALSE);
	}
	return ERROR_TOKEN;
}

static Token next_token(Ctx * ctx) {
	char c;
loop:
	switch (c = lexer_peek(&ctx->lexer)) {
	case ' ':
	case '\n':
	case '\r':
	case '\t':
	case '\v':
		++ctx->lexer.src;
		goto loop;
	case '{':
		++ctx->lexer.src;
		return token_new(TT_LBRACE);
	case '}':
		++ctx->lexer.src;
		return token_new(TT_RBRACE);
	case '[':
		++ctx->lexer.src;
		return token_new(TT_LBRACKET);
	case ']':
		++ctx->lexer.src;
		return token_new(TT_RBRACKET);
	case ',':
		++ctx->lexer.src;
		return token_new(TT_COMMA);
	case ':':
		++ctx->lexer.src;
		return token_new(TT_COLON);
	case '"':
		++ctx->lexer.src;
		return lex_rest_of_string(ctx);
	case '\0':
		return token_new(TT_EOF);
	default:
		if (c_is_digit(c) || c == '-') {
			return lex_number(ctx);
		}
		return lex_identifier(ctx);
	}
}

#define ALLOC(ctx, type) ctx_reallocate(ctx, NULL, 0, sizeof(type))
#define FREE_ARRAY(ctx, ptr, size) ctx_free_array(ctx, ptr, size, sizeof(*(ptr)))
static JSONValue * value(Token t, Ctx * ctx);

/* the function object() for parsing json objects
 * makes use of two helper functions to free it strings
 * and object parameters to help with the complexity of freeing them without
 * creating bugs in its definition.
 * The remove parameter accounts for
 * uninitialized slots at the end while parsing
 */

static void _object_free_strings(Ctx * ctx, char ** strings, size_t size, size_t remove) {
	size_t i;
	for (i = 0; i < size - remove; i++) {
		char * str =  strings[i];
		allocator_free(str, strlen(str) + 1, ctx->allocator);
	}
	FREE_ARRAY(ctx, strings, size);
}

static void _object_free_values(Ctx * ctx, JSONValue ** values, size_t size, size_t remove) {
	size_t i;
	for (i = 0; i < size - remove; i++) {
		JSONValue * value = values[i];
		json_free(value, ctx->allocator);
	}
	FREE_ARRAY(ctx, values, size);
}

static JSONObject * object(Ctx * ctx) {
	char ** strings = NULL;
	JSONValue ** values = NULL;
	size_t count = 0;
	JSONObject * obj;
	Token token;
	for (token = next_token(ctx); token.type != TT_RBRACE; token = next_token(ctx)) {
		JSONValue * nvalue;
		JSONValue ** new_values;
		char ** new_strings = ctx_grow_array(ctx, strings, count, count + 1, sizeof(*strings));
		if (!new_strings) {
			_object_free_strings(ctx, strings, count, 0);
			return NULL;
		}
		new_values = ctx_grow_array(ctx, values, count, count + 1, sizeof(*values));
		if (!new_values) {
			_object_free_strings(ctx, new_strings, count + 1, 1);
			_object_free_values(ctx, values, count, 0);
			return NULL;
		}
		strings = new_strings;
		values = new_values;
		++count;
		if (token.type != TT_STRING) {
			_object_free_strings(ctx, strings, count, 1);
			_object_free_values(ctx, values, count, 1);
			return NULL;
		}
		strings[count - 1] = token.as.string; /* initialized last slot,
				 _object_free_strings remove parameter should be 0 from now on
		 */
		token = next_token(ctx);
		if (token.type != TT_COLON) {
			_object_free_strings(ctx, strings, count, 0);
			_object_free_values(ctx, values, count, 1);
			return NULL;
		}
		nvalue = value(next_token(ctx), ctx);
		if (!nvalue) {
			_object_free_strings(ctx, strings, count, 0);
			_object_free_values(ctx, values, count, 1);
			return NULL;
		}
		values[count - 1] = nvalue;
		token = next_token(ctx);
		if (token.type == TT_RBRACE) {
			break;
		}
		if (token.type != TT_COMMA) {
			_object_free_strings(ctx, strings, count, 0);
			_object_free_values(ctx, values, count, 0);
			return NULL;
		}
	}
	obj = ALLOC(ctx, JSONObject);
	if (!obj) {
		_object_free_strings(ctx, strings, count, 0);
		_object_free_values(ctx, values, count, 0);
		return NULL;
	}
	obj->value.type = JSON_OBJ;
	obj->strings = strings;
	obj->values = values;
	obj->count = count;
	return obj;
}

static JSONArray * array(Ctx * ctx) {
	JSONValue ** values = NULL;
	JSONArray * array;
	size_t size = 0;
	Token t;
	size_t i;
	for (t = next_token(ctx); t.type != TT_RBRACKET; t = next_token(ctx)) {
		JSONValue ** nvalues;
		JSONValue * nvalue = value(t, ctx);
		if (!nvalue) {
			goto error;
		}
		nvalues = ctx_grow_array(ctx, values, size, size + 1, sizeof(*values));
		if (!nvalues) {
			json_free(nvalue, ctx->allocator);
			goto error;
		}
		values = nvalues;
		values[size] = nvalue;
		++size;
		t = next_token(ctx);
		if (t.type == TT_RBRACKET) {
			break;
		}
		if (t.type != TT_COMMA) {
			goto error;
		}
	}
	array = ALLOC(ctx, JSONArray);
	if (!array) {
		goto error;
	}
	array->value.type = JSON_ARRAY;
	array->values = values;
	array->size = size;
	return array;
error:;
	for (i = 0; i < size; i++) {
		json_free(values[i], ctx->allocator);
	}
	FREE_ARRAY(ctx, values, size);
	return NULL;
}

static JSONValue * value(Token t, Ctx * ctx) {
	switch (t.type) {
	case TT_NULL:
		return &json_null;
	case TT_TRUE:
		return &json_true;
	case TT_FALSE:
		return &json_false;
	case TT_STRING: {
		JSONString * str = ALLOC(ctx, JSONString);
		if (!str) {
			return NULL;
		}
		str->value.type = JSON_STRING;
		str->string = t.as.string;
		return (JSONValue *)str;
	}
	case TT_NUMBER: {
		JSONNumber * num = ALLOC(ctx, JSONNumber);
		if (!num) {
			return NULL;
		}
		num->value.type = JSON_NUMBER;
		num->number = t.as.number;
		return (JSONValue *)num;
	}
	case TT_LBRACKET:
		return (JSONValue *)array(ctx);
	case TT_LBRACE:
		return (JSONValue *)object(ctx);
	default:
		return NULL;
	}
}

JSONValue * json_parse(const char * string, JSONAllocator allocator) {
	Ctx ctx;
	ctx.allocator = allocator;
	ctx.lexer = lexer_new(string);
	return value(next_token(&ctx), &ctx);
}


JSONAllocator json_allocator_new(void * ctx, JSONAllocatorCallback callback) {
	JSONAllocator allocator;
	allocator.ctx = ctx;
	allocator.callback = callback;
	return allocator;
}

static void * default_allocator_callback(void * ctx, void * old_alloc, size_t old_size, size_t new_size) {
	(void)ctx;
	(void)old_size;
	return realloc(old_alloc, new_size);
}

JSONAllocator json_default_allocator(void) {
	JSONAllocator allocator;
	allocator.callback = default_allocator_callback;
	/* allocator.ctx does not need to be initialized as the system allocator uses global state */
	return allocator;
}

static void free_object(JSONObject * obj, JSONAllocator allocator) {
	size_t i;
	for (i = 0; i < obj->count; i++) {
		char * string = obj->strings[i];
		JSONValue * value = obj->values[i];
		allocator_free(string, strlen(string) + 1, allocator);
		json_free(value, allocator);
	}
	allocator_free_array(obj->strings, obj->count, sizeof(*obj->strings), allocator);
	allocator_free_array(obj->values, obj->count, sizeof(*obj->values), allocator);
	allocator_free(obj, sizeof(JSONObject), allocator);
}

void json_free(JSONValue * value, JSONAllocator allocator) {
	JSONArray * array;
	JSONString * string;
	size_t i;
	switch (value->type) {
	case JSON_OBJ:
		free_object((JSONObject *)value, allocator);
		break;
	case JSON_ARRAY:
		array = (JSONArray *)value;
		for (i = 0; i < array->size; i++) {
			json_free(array->values[i], allocator);
		}
		allocator_free_array(array->values, array->size, sizeof(*array->values), allocator);
		allocator_free(array, sizeof(JSONArray), allocator);
		break;
	case JSON_STRING:
		string = (JSONString *)value;
		allocator_free(string->string, strlen(string->string) + 1, allocator);
		allocator_free(string, sizeof(JSONString), allocator);
		break;
	case JSON_NUMBER:
		allocator_free(value, sizeof(JSONNumber), allocator);
		break;
	case JSON_BOOL:
	case JSON_NULL:
		break;
	}
}


JSONType json_value_type(const JSONValue * value) {
	return value->type;
}

int	json_value_as_bool(const JSONValue * value) {
	return value == &json_true;
}

double json_value_as_number(const JSONValue * value) {
	return ((JSONNumber *)value)->number;
}

const char * json_value_as_string(const JSONValue * value) {
	return ((JSONString *)value)->string;
}

const JSONArray * json_value_as_array(const JSONValue * value) {
	return (JSONArray *)value;
}

const JSONObject * json_value_as_object(const JSONValue * value) {
	return (JSONObject *)value;
}

const JSONValue * json_array_index(const JSONArray * array, size_t index) {
	return array->values[index];
}

const JSONValue * json_object_get(const JSONObject * obj, const char * key) {
	size_t i;
	for (i = 0; i < obj->count; i++) {
		if (strcmp(key, obj->strings[i]) == 0) {
			return obj->values[i];
		}
	}
	return NULL;
}

size_t json_array_length(const JSONArray * array) {
	return array->size;
}

size_t json_object_count(const JSONObject * obj) {
	return obj->count;
}

static void print_indent(FILE * file, size_t indent) {
	size_t i;
	for (i = 0; i < indent; i++) {
		fputs("  ", file);
	}
}

static void print_value(FILE * file, const JSONValue * value, size_t indent);

static void print_string(FILE * file, const char * str) {
	char c;
	fputc('\"', file);
	for (c = *str; c != '\0'; ++str, c = *str) {
		switch (c) {
			case '\b':
				c = 'b';
				break;
			case '\f':
				c = 'f';
				break;
			case '\n':
				c = 'n';
				break;
			case '\r':
				c = 'r';
				break;
			case '\"':
				c = '\"';
				break;
			case '\\':
				c = '\\';
				break;
			default:
				goto end;
		}
		fputc('\\', file);
end:;
		fputc(c, file);
	}
	fputc('\"', file);
}

static void print_obj(FILE * file, const JSONObject * obj, size_t indent) {
	fputs("{", file);
	if (obj->count > 0)	{
		size_t i = 0;
		for (;;) {
			char * str;
			fputc('\n', file);
			print_indent(file, indent + 1);
			str = obj->strings[i];
			print_string(file, str);
			fputs(" : ", file);
			print_value(file, obj->values[i], indent + 1);
			++i;
			if (i == obj->count) {
				break;
			}
			fputs(",", file);
		}
		fputc('\n', file);
		print_indent(file, indent - 1);
	}
	fputc('}', file);
}

static void print_array(FILE * file, const JSONArray * array, size_t indent) {
	fputs("[", file);
	if (array->size > 0) {
		size_t i;
		for (i = 0;;) {
			fputc('\n', file);
			print_indent(file, indent + 1);
			print_value(file, array->values[i], indent + 1);
			++i;
			if (i == array->size) {
				break;
			}
			fputs(",", file);
		}
		fputc('\n', file);
		print_indent(file, indent - 1);
	}
	fputc(']', file);
}

static void print_value(FILE * file, const JSONValue * value, size_t indent) {
	switch (value->type) {
	case JSON_NULL:
		fputs("null", file);
		break;
	case JSON_BOOL:
		fputs(json_value_as_bool(value) ? "true" : "false", file);
		break;
	case JSON_NUMBER:
		fprintf(file, "%g", json_value_as_number(value));
		break;
	case JSON_STRING:
		print_string(file, json_value_as_string(value));
		break;
	case JSON_ARRAY:
		print_array(file, json_value_as_array(value), indent + 1);
		break;
	case JSON_OBJ:
		print_obj(file, json_value_as_object(value), indent + 1);
		break;
	}
}

int json_print(FILE * file, const JSONValue * value) {
	print_value(file, value, 0);
	putc('\n', file);
	return ferror(file);
}

void print_value_min(FILE * file, const JSONValue * value) {
	char * sep;
	const JSONArray * array;
	const JSONObject * object;
	size_t i;
	switch (value->type) {
	case JSON_NULL:
		fputs("null", file);
		break;
	case JSON_BOOL:
		fputs(json_value_as_bool(value) ? "true" : "false", file);
		break;
	case JSON_NUMBER:
		fprintf(file, "%g", json_value_as_number(value));
		break;
	case JSON_STRING:
		print_string(file, json_value_as_string(value));
		break;
	case JSON_ARRAY:
		fputc('[', file);
		sep = "";
		array = json_value_as_array(value);
		for (i = 0; i < array->size; i++) {
			fputs(sep, file);
			print_value_min(file, array->values[i]);
			sep = ",";
		}
		fputc(']', file);
		break;
	case JSON_OBJ:
		fputc('{', file);
		sep  = "";
		object = json_value_as_object(value);
		for ( i = 0; i < object->count; i++) {
			fputs(sep, file);
			print_string(file, object->strings[i]);
			fputc(':', file);
			print_value_min(file, object->values[i]);
			sep  = ",";
		}
		fputc('}', file);
		break;
	}
}

int json_print_minified(FILE * file, const JSONValue * value) {
	print_value_min(file, value);
	return ferror(file);
}
