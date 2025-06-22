## Small little functional JSON parser library with no dependencies written in C89

# Why?
I wanted to make the project to prove my profeciency in writing parsers in C.

# What does it do?
- The library provides basic JSON parsing and printing.
- It attempts to implement the ECMA-404 JSON specification.
- It is very barebones, lacking support for manual creation or mutation of JSON structures.
- It is does not support streaming style parsing.
- It will not attempt to validate the contents of strings, outside of `\uXXXX` constants.
- It doesn't even attempt to return adequate errors for debugging.
- But comes with support for custom allocators.
- It also permits trailing commas.

# Basic Usage:
```c
    const char * input = /* ... */
    JSONAllocator allocator;
    JSONValue * value;
    allocator = json_default_allocator();
    value = json_parse(input, allocator);
    assert(value);
    json_print(stdout, value); // or json_print_minified(stdout, value)
    json_free(value, allocator);
```
To match on the type of a ``JSONValue``, you can switch on the result of ``json_value_type``, on the value e.g.
```c
switch (json_value_type(value)) {
case JSON_NULL:
    /* ... */
case JSON_BOOL:
    /* ... */
case JSON_NUMBER:
    /* ... */
case JSON_STRING:
    /* ... */
case JSON_ARRAY:
    /* ... */
case JSON_OBJ:
    /* ... */
}
```
The library has a whole host of functions for interacting with JSON values.
```c
JSONType json_value_type(const JSONValue * value);
int json_value_as_bool(const JSONValue * value);
double json_value_as_number(const JSONValue * value);
const char * json_value_as_string(const JSONValue * value);
const JSONArray * json_value_as_array(const JSONValue * value);
const JSONObject * json_value_as_object(const JSONValue * value);

const JSONValue * json_array_as_value(const JSONArray * array);
const JSONValue * json_object_as_value(const JSONObject * obj);

const JSONValue * json_array_index(const JSONArray * array, size_t index);

/* can fail with NULL */
const JSONValue * json_object_get(const JSONObject * obj, const char * key);

size_t json_array_length(const JSONArray * array);
size_t json_object_count(const JSONObject * obj);
```


# Custom Allocators:
You can create a custom allocator to supply to ``json_parse`` with by directly initializing the ``JSONAllocator`` struct or using the ``json_allocator_new`` helper function.
The full definition of the ``JSONAllocator`` structure is below.
```c
typedef void *(* JSONAllocatorCallback)(void * ctx, void * old_alloc, size_t old_size, size_t new_size);

typedef struct JSONAllocator {
	void * ctx;
	JSONAllocatorCallback callback;
} JSONAllocator;
```
The ``JSONAllocatorCallback`` function used, when supplied with the ``ctx`` ptr, is expected to function similarily to the ``realloc`` function, where:
- if ``new_size == 0``, it should act like ``free(old_alloc)``, taking the allocation size in ``old_size``.
- else if ``old_alloc == NULL``, it should act like ``malloc(new_size)``.
- else it exhibit the normal ``realloc`` behavior with ``old_size`` explicitly passed in.

A function used as a ``JSONAllocatorCallback`` is expected to:
1. Provide at least ``void *`` alignment.
2. Not free ``old_alloc`` on reallocation failure.

A function used as a ``JSONAllocatorCallback`` is not expected to:
1. Support being called with ``(_, NULL, _, 0)``

# Building
Should be very straight forward to build. Assuming you have the library in the ``json`` folder, you could do:
```bash
    cc -C json/json.c -o json.o
```
