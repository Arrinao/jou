#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"

struct TypeInfo {
    Type type;
    struct TypeInfo *pointer;  // type that represents a pointer to this type, or NULL
};

static struct {
    bool inited;
    struct TypeInfo integers[65][2];  // integers[i][j] = i-bit integer, j=1 for signed, j=0 for unsigned
    struct TypeInfo boolean, voidptr;
} global_state;

const Type *boolType = &global_state.boolean.type;
const Type *intType = &global_state.integers[32][true].type;
const Type *byteType = &global_state.integers[8][false].type;
const Type *voidPtrType = &global_state.voidptr.type;

void free_type(Type *t)
{
    while(t) {
        if (t->kind == TYPE_STRUCT) {
            free(t->data.structfields.types);
            free(t->data.structfields.names);
        }

        assert(offsetof(struct TypeInfo, type) == 0);
        Type *next = &((struct TypeInfo *)t)->pointer->type;
        free(t);
        t = next;
    }
}

static void free_global_state(void)
{
    assert(global_state.inited);
    free_type(&global_state.boolean.pointer->type);
    free_type(&global_state.voidptr.pointer->type);
    for (int size = 8; size <= 64; size *= 2)
        for (int is_signed = 0; is_signed <= 1; is_signed++)
            free_type(&global_state.integers[size][is_signed].pointer->type);
}

void init_types(void)
{
    assert(!global_state.inited);

    global_state.boolean.type = (Type){ .name = "bool", .kind = TYPE_BOOL };
    global_state.voidptr.type = (Type){ .name = "void*", .kind = TYPE_VOID_POINTER };

    for (int size = 8; size <= 64; size *= 2) {
        global_state.integers[size][true].type.kind = TYPE_SIGNED_INTEGER;
        global_state.integers[size][false].type.kind = TYPE_UNSIGNED_INTEGER;

        for (int is_signed = 0; is_signed <= 1; is_signed++) {
            global_state.integers[size][is_signed].type.data.width_in_bits = size;
            sprintf(
                global_state.integers[size][is_signed].type.name,
                "<%d-bit %s integer>",
                size, is_signed?"signed":"unsigned");
        }
    }

    strcpy(global_state.integers[8][false].type.name, "byte");
    strcpy(global_state.integers[32][true].type.name, "int");

    global_state.inited = true;
    atexit(free_global_state);  // not really necessary, but makes valgrind happier
}

const Type *type_bool(void)
{
    return &global_state.boolean.type;
}

const Type *get_integer_type(int size_in_bits, bool is_signed)
{
    assert(size_in_bits==8 || size_in_bits==16 || size_in_bits==32 || size_in_bits==64);
    return &global_state.integers[size_in_bits][is_signed].type;
}

const Type *get_pointer_type(const Type *t)
{
    assert(offsetof(struct TypeInfo, type) == 0);
    struct TypeInfo *info = (struct TypeInfo *)t;

    if (!info->pointer) {
        info->pointer = calloc(1, sizeof *info->pointer);
        info->pointer->type = (Type){ .kind=TYPE_POINTER, .data.valuetype=t };
        snprintf(info->pointer->type.name, sizeof info->pointer->type.name, "%s*", t->name);
    }
    return &info->pointer->type;
}

bool is_integer_type(const Type *t)
{
    return (t->kind == TYPE_SIGNED_INTEGER || t->kind == TYPE_UNSIGNED_INTEGER);
}

bool is_pointer_type(const Type *t)
{
    return (t->kind == TYPE_POINTER || t->kind == TYPE_VOID_POINTER);
}

const Type *type_of_constant(const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_NULL:
        return voidPtrType;
    case CONSTANT_STRING:
        return get_pointer_type(byteType);
    case CONSTANT_BOOL:
        return type_bool();
    case CONSTANT_INTEGER:
        return get_integer_type(c->data.integer.width_in_bits, c->data.integer.is_signed);
    }
    assert(0);
}

Type *create_struct(const char *name, int fieldcount, char (*fieldnames)[100], const Type **fieldtypes)
{
    struct TypeInfo *result = calloc(1, sizeof *result);
    result->type = (Type){
        .kind = TYPE_STRUCT,
        .data.structfields = {.count=fieldcount, .types=fieldtypes, .names=fieldnames},
    };

    assert(strlen(name) < sizeof result->type.name);
    strcpy(result->type.name, name);

    return &result->type;
}


char *signature_to_string(const Signature *sig, bool include_return_type)
{
    List(char) result = {0};
    AppendStr(&result, sig->funcname);
    Append(&result, '(');

    for (int i = 0; i < sig->nargs; i++) {
        if(i)
            AppendStr(&result, ", ");
        AppendStr(&result, sig->argnames[i]);
        AppendStr(&result, ": ");
        AppendStr(&result, sig->argtypes[i]->name);
    }
    if (sig->takes_varargs) {
        if (sig->nargs)
            AppendStr(&result, ", ");
        AppendStr(&result, "...");
    }
    Append(&result, ')');
    if (include_return_type) {
        AppendStr(&result, " -> ");
        AppendStr(&result, sig->returntype ? sig->returntype->name : "void");
    }
    Append(&result, '\0');
    return result.ptr;
}

Signature copy_signature(const Signature *sig)
{
    Signature result = *sig;

    result.argtypes = malloc(sizeof(result.argtypes[0]) * result.nargs); // NOLINT
    for (int i = 0; i < result.nargs; i++)
        result.argtypes[i] = sig->argtypes[i];

    result.argnames = malloc(sizeof(result.argnames[0]) * result.nargs);
    memcpy(result.argnames, sig->argnames, sizeof(result.argnames[0]) * result.nargs);

    return result;
}
