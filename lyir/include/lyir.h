/*
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2023 Local Atticus
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef LAYEC_H
#define LAYEC_H

#include <stdbool.h>
#include <stdint.h>

#include "lca.h"

// TODO(local): remove Laye stuff from LYIR
typedef struct laye_node laye_node;
typedef struct lyir_dependency_graph lyir_dependency_graph;
#define LAYEC_VERSION "0.1.1"
// TODO(local): remove Laye stuff from LYIR

// TODO(local): probably just include this in source files instead
#define COLCAT(A, B) A##B
#define COL(X)       (use_color ? COLCAT(ANSI_COLOR_, X) : "")
// TODO(local): probably just include this in source files instead

typedef int64_t lyir_sourceid;

typedef struct lyir_source {
    lca_string name;
    lca_string text;
} lyir_source;

typedef struct lyir_target_info {
    int size_of_pointer;
    int align_of_pointer;

    struct {
        int size_of_bool;
        int size_of_char;
        int size_of_short;
        int size_of_int;
        int size_of_long;
        int size_of_long_long;
        int size_of_float;
        int size_of_double;

        int align_of_bool;
        int align_of_char;
        int align_of_short;
        int align_of_int;
        int align_of_long;
        int align_of_long_long;
        int align_of_float;
        int align_of_double;

        bool char_is_signed;
    } ffi;
} lyir_target_info;

extern lyir_target_info* lyir_default_target;
extern lyir_target_info* lyir_x86_64_linux;
extern lyir_target_info* lyir_x86_64_windows;

typedef struct lyir_type lyir_type;
typedef struct lyir_value lyir_value;
typedef struct lyir_module lyir_module;
typedef struct lyir_builder lyir_builder;

typedef struct lyir_struct_member {
    lyir_type* type;
    bool is_padding;
} lyir_struct_member;

typedef void (*lyir_ir_pass_function)(lyir_module* module);

typedef struct lyir_context {
    lca_allocator allocator;
    lyir_target_info* target;

    bool use_color;
    bool has_reported_errors;
    bool use_byte_positions_in_diagnostics;

    lca_da(lyir_source) sources;
    lca_da(lca_string_view) library_directories;
    lca_da(lca_string_view) link_libraries;

    int64_t max_interned_string_size;
    lca_arena* string_arena;
    lca_da(lca_string) _interned_strings;
    lca_da(lca_string) allocated_strings;
    lca_da(lyir_module*) ir_modules;

    lca_da(lyir_dependency_graph*) _all_depgraphs;

    lca_arena* type_arena;
    lca_da(lyir_type*) _all_types;

    struct {
        lyir_type* poison;
        lyir_type* ptr;
        lyir_type* _void;
        lca_da(lyir_type*) int_types;
        lyir_type* f32;
        lyir_type* f64;
    } types;

    struct {
        lyir_value* _void;
    } values;

    lca_da(lyir_value*) _all_values;
    lca_da(lyir_type*) _all_struct_types;
} lyir_context;

typedef struct lyir_location {
    lyir_sourceid sourceid;
    int64_t offset;
    int64_t length;
} lyir_location;

typedef enum lyir_status {
    LYIR_NO_STATUS,
    LYIR_INFO,
    LYIR_NOTE,
    LYIR_WARN,
    LYIR_ERROR,
    LYIR_FATAL,
    LYIR_ICE,
} lyir_status;

typedef struct lyir_diag {
    lyir_status status;
    lyir_location location;
    lca_string message;
} lyir_diag;

typedef enum lyir_value_category {
    LYIR_RVALUE,
    LYIR_LVALUE,
} lyir_value_category;

typedef enum lyir_mangling {
    // Whatever the default is for the declaration.
    LYIR_MANGLE_DEFAULT,
    // Explicitly no mangling.
    LYIR_MANGLE_NONE,
    // Laye-style name mangling.
    LYIR_MANGLE_LAYE,
} lyir_mangling;

typedef enum lyir_linkage {
    // Local variable.
    LYIR_LINK_LOCAL,
    // Not exported, has definition.
    LYIR_LINK_INTERNAL,
    // Imported from another module, no definition.
    LYIR_LINK_IMPORTED,
    // Exported, has definition.
    LYIR_LINK_EXPORTED,
    // Imported and exported, no definition.
    LYIR_LINK_REEXPORTED,
} lyir_linkage;

typedef enum lyir_calling_convention {
    // Whatever the default is for the declaration or type.
    LYIR_DEFAULTCC,
    // Explicitly `cdecl` calling convention.
    LYIR_CCC,
    // Laye calling convention.
    LYIR_LAYECC,
} lyir_calling_convention;

typedef enum lyir_sema_state {
    // this node has not yet been analysed by the semantic analyser.
    LYIR_SEMA_NOT_ANALYSED,
    // semantic analysis for this node has started, but is not complete.
    LYIR_SEMA_IN_PROGRESS,
    // semantic analysis completed without errors.
    LYIR_SEMA_DONE,
} lyir_sema_state;

typedef enum lyir_evaluated_constant_kind {
    LYIR_EVAL_NULL,
    LYIR_EVAL_VOID,
    LYIR_EVAL_BOOL,
    LYIR_EVAL_INT,
    LYIR_EVAL_FLOAT,
    LYIR_EVAL_STRING,
} lyir_evaluated_constant_kind;

typedef struct lyir_evaluated_constant {
    lyir_evaluated_constant_kind kind;
    union {
        bool bool_value;
        int64_t int_value;
        double float_value;
        lca_string_view string_value;
    };
} lyir_evaluated_constant;

typedef void lyir_dependency_entity;

typedef struct lyir_dependency_entry {
    void* node;
    lca_da(lyir_dependency_entity*) dependencies;
} lyir_dependency_entry;

struct lyir_dependency_graph {
    lyir_context* context;
    lca_arena* arena;
    lca_da(lyir_dependency_entry*) entries;
};

typedef struct lyir_dependency_order_result {
    enum {
        LYIR_DEP_OK,
        LYIR_DEP_CYCLE,
    } status;

    union {
        lca_da(lyir_dependency_entity*) ordered_entities;

        struct {
            lyir_dependency_entity* from;
            lyir_dependency_entity* to;
        };
    };
} lyir_dependency_order_result;

typedef enum lyir_type_kind {
    LYIR_TYPE_POINTER,
    LYIR_TYPE_VOID,
    LYIR_TYPE_ARRAY,
    LYIR_TYPE_FUNCTION,
    LYIR_TYPE_INTEGER,
    LYIR_TYPE_FLOAT,
    LYIR_TYPE_STRUCT,
} lyir_type_kind;

typedef enum lyir_builtin_kind {
    LYIR_BUILTIN_DEBUGTRAP,
    LYIR_BUILTIN_FILENAME,
    LYIR_BUILTIN_INLINE,
    LYIR_BUILTIN_LINE,
    LYIR_BUILTIN_MEMCOPY,
    LYIR_BUILTIN_MEMSET,
    LYIR_BUILTIN_SYSCALL,
} lyir_builtin_kind;

typedef enum lyir_value_kind {
    LYIR_IR_INVALID,

    // Values
    LYIR_IR_INTEGER_CONSTANT,
    LYIR_IR_FLOAT_CONSTANT,
    LYIR_IR_ARRAY_CONSTANT,
    LYIR_IR_VOID_CONSTANT,
    LYIR_IR_POISON,

    // Values that track their users.
    LYIR_IR_BLOCK,
    LYIR_IR_FUNCTION,
    LYIR_IR_GLOBAL_VARIABLE,
    LYIR_IR_PARAMETER,

    // Instructions
    LYIR_IR_NOP,
    LYIR_IR_ALLOCA,
    LYIR_IR_CALL,
    LYIR_IR_PTRADD,
    LYIR_IR_BUILTIN,
    LYIR_IR_LOAD,
    LYIR_IR_PHI,
    LYIR_IR_STORE,

    // Terminators
    LYIR_IR_BRANCH,
    LYIR_IR_COND_BRANCH,
    LYIR_IR_RETURN,
    LYIR_IR_UNREACHABLE,

    // Unary instructions
    LYIR_IR_ZEXT,
    LYIR_IR_SEXT,
    LYIR_IR_TRUNC,
    LYIR_IR_BITCAST,
    LYIR_IR_NEG,
    LYIR_IR_COPY,
    LYIR_IR_COMPL,
    LYIR_IR_FPTOUI,
    LYIR_IR_FPTOSI,
    LYIR_IR_UITOFP,
    LYIR_IR_SITOFP,
    LYIR_IR_FPTRUNC,
    LYIR_IR_FPEXT,

    // Binary instructions
    LYIR_IR_ADD,
    LYIR_IR_FADD,
    LYIR_IR_SUB,
    LYIR_IR_FSUB,
    LYIR_IR_MUL,
    LYIR_IR_FMUL,
    LYIR_IR_SDIV,
    LYIR_IR_UDIV,
    LYIR_IR_FDIV,
    LYIR_IR_SMOD,
    LYIR_IR_UMOD,
    LYIR_IR_FMOD,
    LYIR_IR_SHL,
    LYIR_IR_SAR,
    LYIR_IR_SHR,
    LYIR_IR_AND,
    LYIR_IR_OR,
    LYIR_IR_XOR,

    // Integer comparison instructions
    LYIR_IR_ICMP_EQ,
    LYIR_IR_ICMP_NE,
    // Signed comparisons
    LYIR_IR_ICMP_SLT,
    LYIR_IR_ICMP_SLE,
    LYIR_IR_ICMP_SGT,
    LYIR_IR_ICMP_SGE,
    // Unsigned comparisons
    LYIR_IR_ICMP_ULT,
    LYIR_IR_ICMP_ULE,
    LYIR_IR_ICMP_UGT,
    LYIR_IR_ICMP_UGE,

    // Float comparison instructions
    LYIR_IR_FCMP_FALSE,
    LYIR_IR_FCMP_OEQ,
    LYIR_IR_FCMP_OGT,
    LYIR_IR_FCMP_OGE,
    LYIR_IR_FCMP_OLT,
    LYIR_IR_FCMP_OLE,
    LYIR_IR_FCMP_ONE,
    LYIR_IR_FCMP_ORD,
    LYIR_IR_FCMP_UEQ,
    LYIR_IR_FCMP_UGT,
    LYIR_IR_FCMP_UGE,
    LYIR_IR_FCMP_ULT,
    LYIR_IR_FCMP_ULE,
    LYIR_IR_FCMP_UNE,
    LYIR_IR_FCMP_UNO,
    LYIR_IR_FCMP_TRUE,
} lyir_value_kind;

// ========== Context ==========

lyir_location lyir_location_combine(lyir_location a, lyir_location b);

void lyir_init_targets(lca_allocator allocator);

lyir_context* lyir_context_create(lca_allocator allocator);
void lyir_context_destroy(lyir_context* context);

lyir_sourceid lyir_context_get_or_add_source_from_file(lyir_context* context, lca_string_view file_path);
lyir_sourceid lyir_context_get_or_add_source_from_string(lyir_context* context, lca_string name, lca_string source_text);
lyir_source lyir_context_get_source(lyir_context* context, lyir_sourceid sourceid);

bool lyir_context_get_location_info(lyir_context* context, lyir_location location, lca_string_view* out_name, int64_t* out_line, int64_t* out_column);
void lyir_context_print_location_info(lyir_context* context, lyir_location location, lyir_status status, FILE* stream, bool use_color);

lyir_diag lyir_info(lyir_context* context, lyir_location location, const char* format, ...);
lyir_diag lyir_note(lyir_context* context, lyir_location location, const char* format, ...);
lyir_diag lyir_warn(lyir_context* context, lyir_location location, const char* format, ...);
lyir_diag lyir_error(lyir_context* context, lyir_location location, const char* format, ...);
lyir_diag lyir_ice(lyir_context* context, lyir_location location, const char* format, ...);

void lyir_write_diag(lyir_context* context, lyir_diag diag);
void lyir_write_info(lyir_context* context, lyir_location location, const char* format, ...);
void lyir_write_note(lyir_context* context, lyir_location location, const char* format, ...);
void lyir_write_warn(lyir_context* context, lyir_location location, const char* format, ...);
void lyir_write_error(lyir_context* context, lyir_location location, const char* format, ...);
void lyir_write_ice(lyir_context* context, lyir_location location, const char* format, ...);

lca_string_view lyir_context_intern_string_view(lyir_context* context, lca_string_view s);

#define LYIR_ICE(C, L, F) do { lyir_write_ice(C, L, F); abort(); } while (0)
#define LYIR_ICEV(C, L, F, ...) do { lyir_write_ice(C, L, F, __VA_ARGS__); abort(); } while (0)

// ========== Shared Data ==========

const char* layec_status_to_cstring(lyir_status status);
const char* layec_value_category_to_cstring(lyir_value_category category);

bool lyir_evaluated_constant_equals(lyir_evaluated_constant a, lyir_evaluated_constant b);

lyir_dependency_graph* lyir_dependency_graph_create_in_context(lyir_context* context);
void lyir_dependency_graph_destroy(lyir_dependency_graph* graph);
void lyir_depgraph_add_dependency(lyir_dependency_graph* graph, lyir_dependency_entity* node, lyir_dependency_entity* dependency);
void lyir_depgraph_ensure_tracked(lyir_dependency_graph* graph, lyir_dependency_entity* node);
lyir_dependency_order_result lyir_dependency_graph_get_ordered_entities(lyir_dependency_graph* graph);

int lyir_get_significant_bits(int64_t value);

// ========== IR ==========

void lyir_irpass_validate(lyir_module* module);
void lyir_irpass_fix_abi(lyir_module* module);

// TODO(local): backends as separate library APIs? lyir-llvm.h for example?
lca_string lyir_codegen_c(lyir_module* module);
lca_string lyir_codegen_llvm(lyir_module* module);

// Context API

int64_t lyir_context_get_struct_type_count(lyir_context* context);
lyir_type* lyir_context_get_struct_type_at_index(lyir_context* context, int64_t index);

// Module API

lyir_module* lyir_module_create(lyir_context* context, lca_string_view module_name);
void lyir_module_destroy(lyir_module* module);
lyir_value* lyir_module_create_function(lyir_module* module, lyir_location location, lca_string_view function_name, lyir_type* function_type, lca_da(lyir_value*) parameters, lyir_linkage linkage);

lyir_context* lyir_module_context(lyir_module* module);
lca_string_view lyir_module_name(lyir_module* module);
int64_t lyir_module_global_count(lyir_module* module);
lyir_value* lyir_module_get_global_at_index(lyir_module* module, int64_t global_index);
int64_t lyir_module_function_count(lyir_module* module);
lyir_value* lyir_module_get_function_at_index(lyir_module* module, int64_t function_index);
lyir_value* lyir_module_create_global_string_ptr(lyir_module* module, lyir_location location, lca_string_view string_value);

lca_string lyir_module_print(lyir_module* module, bool use_color);

// Type API

const char* lyir_type_kind_to_cstring(lyir_type_kind kind);

lyir_type_kind lyir_type_kind_get(lyir_type* type);

lyir_type* lyir_void_type(lyir_context* context);
lyir_type* lyir_ptr_type(lyir_context* context);
lyir_type* lyir_int_type(lyir_context* context, int bit_width);
lyir_type* lyir_float_type(lyir_context* context, int bit_width);
lyir_type* lyir_array_type(lyir_context* context, int64_t length, lyir_type* element_type);
lyir_type* lyir_function_type(
    lyir_context* context,
    lyir_type* return_type,
    lca_da(lyir_type*) parameter_types,
    lyir_calling_convention calling_convention,
    bool is_variadic
);
lyir_type* lyir_struct_type(lyir_context* context, lca_string_view name, lca_da(lyir_struct_member) members);

bool lyir_type_is_ptr(lyir_type* type);
bool lyir_type_is_void(lyir_type* type);
bool lyir_type_is_array(lyir_type* type);
bool lyir_type_is_function(lyir_type* type);
bool lyir_type_is_integer(lyir_type* type);
bool lyir_type_is_float(lyir_type* type);
bool lyir_type_is_struct(lyir_type* type);

int lyir_type_size_in_bits(lyir_type* type);
int lyir_type_size_in_bytes(lyir_type* type);
int lyir_type_align_in_bits(lyir_type* type);
int lyir_type_align_in_bytes(lyir_type* type);

lyir_type* lyir_type_element_type_get(lyir_type* type);
int64_t lyir_type_array_length_get(lyir_type* type);

bool lyir_type_struct_is_named(lyir_type* type);
lca_string_view lyir_type_struct_name_get(lyir_type* type);
int64_t lyir_type_struct_member_count_get(lyir_type* type);
lyir_struct_member lyir_type_struct_member_get_at_index(lyir_type* type, int64_t index);
lyir_type* lyir_type_struct_member_type_get_at_index(lyir_type* type, int64_t index);

void lyir_type_print_to_string(lyir_type* type, lca_string* s, bool use_color);

// - Function Type API

int64_t lyir_function_type_parameter_count_get(lyir_type* function_type);
lyir_type* lyir_function_type_parameter_type_get_at_index(lyir_type* function_type, int64_t parameter_index);
bool lyir_function_type_is_variadic(lyir_type* function_type);
void lyir_function_type_parameter_type_set_at_index(lyir_type* function_type, int64_t parameter_index, lyir_type* param_type);

// Value API

const char* lyir_value_kind_to_cstring(lyir_value_kind kind);

int64_t lyir_value_user_count_get(lyir_value* value);
lyir_value* lyir_value_user_get_at_index(lyir_value* value, int64_t user_index);

int64_t lyir_value_integer_constant_get(lyir_value* value);
double lyir_value_float_constant_get(lyir_value* value);

lyir_value_kind lyir_value_kind_get(lyir_value* value);
lyir_context* lyir_value_context_get(lyir_value* value);
lyir_location lyir_value_location_get(lyir_value* value);
lyir_linkage lyir_value_linkage_get(lyir_value* value);
lyir_type* lyir_value_type_get(lyir_value* value);
void lyir_value_type_set(lyir_value* value, lyir_type* type);
lca_string_view lyir_value_name_get(lyir_value* value);
int64_t lyir_value_index_get(lyir_value* value);

bool lyir_value_is_terminator(lyir_value* instruction);
bool lyir_value_is_block(lyir_value* value);
bool lyir_value_is_function(lyir_value* value);
bool lyir_value_is_instruction(lyir_value* value);

lyir_value* lyir_void_constant_create(lyir_context* context);
lyir_value* lyir_int_constant_create(lyir_context* context, lyir_location location, lyir_type* type, int64_t value);
lyir_value* lyir_float_constant_create(lyir_context* context, lyir_location location, lyir_type* type, double value);
lyir_value* lyir_array_constant_create(lyir_context* context, lyir_location location, lyir_type* type, void* data, int64_t length, bool is_string_literal);

bool lyir_array_constant_is_string(lyir_value* array_constant);
int64_t lyir_array_constant_length_get(lyir_value* array_constant);
const char* lyir_array_constant_data_get(lyir_value* array_constant);

void lyir_value_print_to_string(lyir_value* value, lca_string* s, bool print_type, bool use_color);

// - Function Value API

lca_string_view lyir_value_function_name_get(lyir_value* function);
lyir_type* lyir_value_function_return_type_get(lyir_value* function);
int64_t lyir_value_function_block_count_get(lyir_value* function);
lyir_value* lyir_value_function_block_get_at_index(lyir_value* function, int64_t block_index);
int64_t lyir_value_function_parameter_count_get(lyir_value* function);
lyir_value* lyir_value_function_parameter_get_at_index(lyir_value* function, int64_t parameter_index);
bool lyir_value_function_is_variadic(lyir_value* function);
void lyir_value_function_parameter_type_set_at_index(lyir_value* function, int64_t parameter_index, lyir_type* param_type);

lyir_value* lyir_value_function_block_append(lyir_value* function, lca_string_view name);

// - Block API

bool lyir_value_block_has_name(lyir_value* block);
lca_string_view lyir_value_block_name_get(lyir_value* block);
int64_t lyir_value_block_index_get(lyir_value* block);
int64_t lyir_value_block_instruction_count_get(lyir_value* block);
lyir_value* lyir_value_block_instruction_get_at_index(lyir_value* block, int64_t instruction_index);
bool lyir_value_block_is_terminated(lyir_value* block);

// - Instruction API

lyir_builtin_kind lyir_value_builtin_kind_get(lyir_value* instruction);

bool lyir_value_global_is_string(lyir_value* global);

bool lyir_value_return_has_value(lyir_value* _return);
lyir_value* lyir_value_return_value_get(lyir_value* _return);

lyir_type* lyir_value_alloca_type_get(lyir_value* alloca);

lyir_value* lyir_value_address_get(lyir_value* instruction);
lyir_value* lyir_value_operand_get(lyir_value* instruction);
lyir_value* lyir_value_lhs_get(lyir_value* instruction);
lyir_value* lyir_value_rhs_get(lyir_value* instruction);
lyir_value* lyir_value_branch_pass_get(lyir_value* instruction);
lyir_value* lyir_value_branch_fail_get(lyir_value* instruction);

lyir_value* lyir_value_callee_get(lyir_value* call);
int64_t lyir_value_call_argument_count_get(lyir_value* call);
lyir_value* lyir_value_call_argument_get_at_index(lyir_value* call, int64_t argument_index);
void lyir_value_call_arguments_set(lyir_value* call, lca_da(lyir_value*) arguments);
int64_t lyir_value_builtin_argument_count_get(lyir_value* builtin);
lyir_value* lyir_value_builtin_argument_set_at_index(lyir_value* builtin, int64_t argument_index);

void lyir_value_phi_incoming_value_add(lyir_value* phi, lyir_value* value, lyir_value* block);
int64_t lyir_value_phi_incoming_value_count_get(lyir_value* phi);
lyir_value* lyir_phi_incoming_value_get_at_index(lyir_value* phi, int64_t index);
lyir_value* lyir_phi_incoming_block_get_at_index(lyir_value* phi, int64_t index);

// Builder API

lyir_builder* lyir_builder_create(lyir_context* context);
void lyir_builder_destroy(lyir_builder* builder);
lyir_module* lyir_builder_module_get(lyir_builder* builder);
lyir_context* lyir_builder_context_get(lyir_builder* builder);
lyir_value* lyir_builder_function_get(lyir_builder* builder);

// Unsets this builder's insert position, so no further insertions will be allowed
// until another call to `layec_builder_position_*` is called.
void lyir_builder_reset(lyir_builder* builder);
void lyir_builder_position_before(lyir_builder* builder, lyir_value* instruction);
void lyir_builder_position_after(lyir_builder* builder, lyir_value* instruction);
void lyir_builder_position_at_end(lyir_builder* builder, lyir_value* block);
lyir_value* lyir_builder_insert_block_get(lyir_builder* builder);
void lyir_builder_insert(lyir_builder* builder, lyir_value* instruction);
void lyir_builder_insert_with_name(lyir_builder* builder, lyir_value* instruction, lca_string_view name);

lyir_value* lyir_value_parameter_create(lyir_module* module, lyir_location location, lyir_type* type, lca_string_view name, int64_t index);

lyir_value* lyir_build_nop(lyir_builder* builder, lyir_location location);
lyir_value* lyir_build_return(lyir_builder* builder, lyir_location location, lyir_value* value);
lyir_value* lyir_build_return_void(lyir_builder* builder, lyir_location location);
lyir_value* lyir_build_unreachable(lyir_builder* builder, lyir_location location);
lyir_value* lyir_build_alloca(lyir_builder* builder, lyir_location location, lyir_type* element_type, int64_t count);
lyir_value* lyir_build_call(lyir_builder* builder, lyir_location location, lyir_value* callee, lyir_type* callee_type, lca_da(lyir_value*) arguments, lca_string_view name);
lyir_value* lyir_build_store(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_value* value);
lyir_value* lyir_build_load(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_type* type);
lyir_value* lyir_build_branch(lyir_builder* builder, lyir_location location, lyir_value* block);
lyir_value* lyir_build_branch_conditional(lyir_builder* builder, lyir_location location, lyir_value* condition, lyir_value* pass_block, lyir_value* fail_block);
lyir_value* lyir_build_phi(lyir_builder* builder, lyir_location location, lyir_type* type);
lyir_value* lyir_build_bitcast(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type);
lyir_value* lyir_build_sign_extend(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type);
lyir_value* lyir_build_zero_extend(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type);
lyir_value* lyir_build_truncate(lyir_builder* builder, lyir_location location, lyir_value* value, lyir_type* type);
lyir_value* lyir_build_icmp_eq(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_ne(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_slt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_ult(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_sle(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_ule(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_sgt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_ugt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_sge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_icmp_uge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_false(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_oeq(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ogt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_oge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_olt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ole(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_one(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ord(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ueq(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ugt(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_uge(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ult(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_ule(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_une(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_uno(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fcmp_true(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_add(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fadd(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_sub(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fsub(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_mul(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fmul(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_sdiv(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_udiv(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fdiv(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_smod(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_umod(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_fmod(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_and(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_or(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_xor(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_shl(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_shr(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_sar(lyir_builder* builder, lyir_location location, lyir_value* lhs, lyir_value* rhs);
lyir_value* lyir_build_neg(lyir_builder* builder, lyir_location location, lyir_value* operand);
lyir_value* lyir_build_compl(lyir_builder* builder, lyir_location location, lyir_value* operand);
lyir_value* lyir_build_fptoui(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to);
lyir_value* lyir_build_fptosi(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to);
lyir_value* lyir_build_uitofp(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to);
lyir_value* lyir_build_sitofp(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to);
lyir_value* lyir_build_fpext(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to);
lyir_value* lyir_build_fptrunc(lyir_builder* builder, lyir_location location, lyir_value* operand, lyir_type* to);
lyir_value* lyir_build_builtin_memset(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_value* value, lyir_value* count);
lyir_value* lyir_build_builtin_memcpy(lyir_builder* builder, lyir_location location, lyir_value* source_address, lyir_value* dest_address, lyir_value* count);
lyir_value* lyir_build_ptradd(lyir_builder* builder, lyir_location location, lyir_value* address, lyir_value* offset_value);

#endif // LAYEC_H
