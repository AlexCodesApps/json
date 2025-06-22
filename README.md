## Small little functional JSON parser library written in C89

# Why?
I wanted to make the project to prove I am proficient at writing parsers in C.

# What does it do?
- The library provides basic JSON parsing and printing.
- It is very barebones, lacking support for creating, mutating JSON.
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
