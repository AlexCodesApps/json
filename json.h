#ifndef LIB_JSON_H
#define LIB_JSON_H

#include <stdio.h>

typedef struct JSONValue JSONValue;
typedef struct JSONObject JSONObject;
typedef struct JSONArray JSONArray;

typedef enum JSONType {
	JSON_NULL,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJ
} JSONType;

typedef void *(* JSONAllocatorCallback)(void * ctx, void * old_alloc, size_t old_size, size_t new_size);

typedef struct JSONAllocator {
	void * ctx;
	JSONAllocatorCallback callback;
} JSONAllocator;

JSONAllocator json_allocator_new(void * ctx, JSONAllocatorCallback callback);
JSONAllocator json_default_allocator(void);
JSONValue * json_parse(const char * string, JSONAllocator allocator);
void json_free(JSONValue * value, JSONAllocator allocator);

JSONType json_value_type(const JSONValue * value);
int	json_value_as_bool(const JSONValue * value);
double json_value_as_number(const JSONValue * value);
const char * json_value_as_string(const JSONValue * value);
const JSONArray * json_value_as_array(const JSONValue * value);
const JSONObject * json_value_as_object(const JSONValue * value);

const JSONValue * json_array_index(const JSONArray * array, size_t index);
const JSONValue * json_object_get(const JSONObject * obj, const char * key);

size_t json_array_length(const JSONArray * array);
size_t json_object_count(const JSONObject * obj);

int json_print(FILE * file, const JSONValue * value);
int json_print_minified(FILE * file, const JSONValue * value);

#endif
