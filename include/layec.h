#ifndef LAYEC_H
#define LAYEC_H

#include "ansi.h"
#include "lcads.h"
#include "lcamem.h"
#include "lcastr.h"

#include <stdbool.h>
#include <stdint.h>

#define LAYEC_VERSION "0.1.1"

#define COLCAT(A, B) A##B
#define COL(X)       (use_color ? COLCAT(ANSI_COLOR_, X) : "")

typedef int64_t layec_sourceid;

typedef struct layec_source {
    string name;
    string text;
} layec_source;

typedef struct layec_target_info {
    int size_of_pointer;
    int align_of_pointer;

    struct {
        int size_of_bool;
        int size_of_char;
        int size_of_short;
        int size_of_int;
        int size_of_long;
        int size_of_long_long;

        int align_of_bool;
        int align_of_char;
        int align_of_short;
        int align_of_int;
        int align_of_long;
        int align_of_long_long;

        bool char_is_signed;
    } c;

    struct {
        int size_of_bool;
        int size_of_int;
        int size_of_float;

        int align_of_bool;
        int align_of_int;
        int align_of_float;
    } laye;
} layec_target_info;

extern layec_target_info* layec_default_target;
extern layec_target_info* layec_x86_64_linux;
extern layec_target_info* layec_x86_64_windows;

typedef struct laye_node laye_node;
typedef struct layec_dependency_graph layec_dependency_graph;

typedef struct layec_type layec_type;
typedef struct layec_value layec_value;
typedef struct layec_module layec_module;
typedef struct layec_builder layec_builder;

typedef void (*layec_ir_pass_function)(layec_module* module);

typedef struct layec_context {
    lca_allocator allocator;
    layec_target_info* target;

    bool use_color;
    bool has_reported_errors;

    dynarr(layec_source) sources;
    dynarr(string) include_directories;

    int64_t max_interned_string_size;
    lca_arena* string_arena;
    dynarr(string) _interned_strings;
    dynarr(string) allocated_strings;

    // types for use in Laye semantic analysis.
    // should not be stored within syntax nodes that have explicit
    // type syntax in the source code, since source location information
    // should be preserved whenever possible. These types are more
    // useful for known type conversions, like type checking the condition
    // of an if statement or for loop to be convertible to type `bool`, or
    // when converting array indices into a platform integer type.
    struct {
        laye_node* poison;
        laye_node* unknown;
        laye_node* type;
        laye_node* _void;
        laye_node* noreturn;
        laye_node* _bool;
        laye_node* _int;
        laye_node* _uint;
        laye_node* _float;
    } laye_types;

    layec_dependency_graph* laye_dependencies;
    dynarr(layec_dependency_graph*) _all_depgraphs;

    lca_arena* type_arena;
    dynarr(layec_type*) _all_types;

    struct {
        layec_type* poison;
        layec_type* ptr;
        layec_type* _void;
        dynarr(layec_type*) int_types;
    } types;

    struct {
        layec_value* _void;
    } values;

    dynarr(layec_value*) _all_values;
} layec_context;

typedef struct layec_location {
    layec_sourceid sourceid;
    int64_t offset;
    int64_t length;
} layec_location;

typedef enum layec_status {
    LAYEC_NO_STATUS,
    LAYEC_INFO,
    LAYEC_NOTE,
    LAYEC_WARN,
    LAYEC_ERROR,
    LAYEC_FATAL,
    LAYEC_ICE,
} layec_status;

typedef struct layec_diag {
    layec_status status;
    layec_location location;
    string message;
} layec_diag;

typedef enum layec_value_category {
    LAYEC_RVALUE,
    LAYEC_LVALUE,
} layec_value_category;

typedef enum layec_mangling {
    // Whatever the default is for the declaration.
    LAYEC_MANGLE_DEFAULT,
    // Explicitly no mangling.
    LAYEC_MANGLE_NONE,
    // Laye-style name mangling.
    LAYEC_MANGLE_LAYE,
} layec_mangling;

typedef enum layec_linkage {
    // Local variable.
    LAYEC_LINK_LOCAL,
    // Not exported, has definition.
    LAYEC_LINK_INTERNAL,
    // Imported from another module, no definition.
    LAYEC_LINK_IMPORTED,
    // Exported, has definition.
    LAYEC_LINK_EXPORTED,
    // Imported and exported, no definition.
    LAYEC_LINK_REEXPORTED,
} layec_linkage;

typedef enum layec_calling_convention {
    // Whatever the default is for the declaration or type.
    LAYEC_DEFAULTCC,
    // Explicitly `cdecl` calling convention.
    LAYEC_CCC,
    // Laye calling convention.
    LAYEC_LAYECC,
} layec_calling_convention;

typedef enum layec_sema_state {
    // this node has not yet been analysed by the semantic analyser.
    LAYEC_SEMA_NOT_ANALYSED,
    // semantic analysis for this node has started, but is not complete.
    LAYEC_SEMA_IN_PROGRESS,
    // semantic analysis resulted in at least one error. analysis is complete, but not "ok".
    LAYEC_SEMA_ERRORED,
    // semantic analysis completed without errors.
    LAYEC_SEMA_OK,
} layec_sema_state;

typedef enum layec_evaluated_constant_kind {
    LAYEC_EVAL_NULL,
    LAYEC_EVAL_VOID,
    LAYEC_EVAL_BOOL,
    LAYEC_EVAL_INT,
    LAYEC_EVAL_FLOAT,
    LAYEC_EVAL_STRING,
} layec_evaluated_constant_kind;

typedef struct layec_evaluated_constant {
    layec_evaluated_constant_kind kind;
    union {
        bool bool_value;
        int64_t int_value;
        double float_value;
        string string_value;
    };
} layec_evaluated_constant;

typedef void layec_dependency_entity;

typedef struct layec_dependency_entry {
    laye_node* node;
    dynarr(layec_dependency_entity*) dependencies;
} layec_dependency_entry;

struct layec_dependency_graph {
    layec_context* context;
    lca_arena* arena;
    dynarr(layec_dependency_entry*) entries;
};

typedef struct layec_dependency_order_result {
    enum {
        LAYEC_DEP_OK,
        LAYEC_DEP_CYCLE,
    } status;

    union {
        dynarr(layec_dependency_entity*) ordered_entities;

        struct {
            layec_dependency_entity* from;
            layec_dependency_entity* to;
        };
    };
} layec_dependency_order_result;

typedef enum layec_type_kind {
    LAYEC_TYPE_POINTER,
    LAYEC_TYPE_VOID,
    LAYEC_TYPE_ARRAY,
    LAYEC_TYPE_FUNCTION,
    LAYEC_TYPE_INTEGER,
    LAYEC_TYPE_FLOAT,
    LAYEC_TYPE_STRUCT,
} layec_type_kind;

typedef enum layec_value_kind {
    LAYEC_IR_INVALID,

    // Values
    LAYEC_IR_INTEGER_CONSTANT,
    LAYEC_IR_ARRAY_CONSTANT,
    LAYEC_IR_VOID_CONSTANT,
    LAYEC_IR_POISON,

    // Values that track their users.
    LAYEC_IR_BLOCK,
    LAYEC_IR_FUNCTION,
    LAYEC_IR_GLOBAL_VARIABLE,
    LAYEC_IR_PARAMETER,

    // Instructions
    LAYEC_IR_ALLOCA,
    LAYEC_IR_CALL,
    LAYEC_IR_GET_ELEMENT_PTR,
    LAYEC_IR_GET_MEMBER_PTR,
    LAYEC_IR_INTRINSIC,
    LAYEC_IR_LOAD,
    LAYEC_IR_PHI,
    LAYEC_IR_STORE,

    // Terminators
    LAYEC_IR_BRANCH,
    LAYEC_IR_COND_BRANCH,
    LAYEC_IR_RETURN,
    LAYEC_IR_UNREACHABLE,

    // Unary instructions
    LAYEC_IR_ZEXT,
    LAYEC_IR_SEXT,
    LAYEC_IR_TRUNC,
    LAYEC_IR_BITCAST,
    LAYEC_IR_NEG,
    LAYEC_IR_COPY,
    LAYEC_IR_COMPL,

    // Binary instructions
    LAYEC_IR_ADD,
    LAYEC_IR_SUB,
    LAYEC_IR_MUL,
    LAYEC_IR_SDIV,
    LAYEC_IR_UDIV,
    LAYEC_IR_SREM,
    LAYEC_IR_UREM,
    LAYEC_IR_SHL,
    LAYEC_IR_SAR,
    LAYEC_IR_SHR,
    LAYEC_IR_AND,
    LAYEC_IR_OR,
    LAYEC_IR_XOR,

    // Comparison instructions
    LAYEC_IR_EQ,
    LAYEC_IR_NE,
    // Signed comparisons
    LAYEC_IR_SLT,
    LAYEC_IR_SLE,
    LAYEC_IR_SGT,
    LAYEC_IR_SGE,
    // Unsigned comparisons
    LAYEC_IR_ULT,
    LAYEC_IR_ULE,
    LAYEC_IR_UGT,
    LAYEC_IR_UGE,
} layec_value_kind;

// ========== Context ==========

void layec_init_targets(lca_allocator allocator);

layec_context* layec_context_create(lca_allocator allocator);
void layec_context_destroy(layec_context* context);

layec_sourceid layec_context_get_or_add_source_from_file(layec_context* context, string_view file_path);
layec_source layec_context_get_source(layec_context* context, layec_sourceid sourceid);

bool layec_context_get_location_info(layec_context* context, layec_location location, string_view* out_name, int64_t* out_line, int64_t* out_column);
void layec_context_print_location_info(layec_context* context, layec_location location, layec_status status, FILE* stream, bool use_color);

layec_diag layec_info(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_note(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_warn(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_error(layec_context* context, layec_location location, const char* format, ...);
layec_diag layec_ice(layec_context* context, layec_location location, const char* format, ...);

void layec_write_diag(layec_context* context, layec_diag diag);
void layec_write_info(layec_context* context, layec_location location, const char* format, ...);
void layec_write_note(layec_context* context, layec_location location, const char* format, ...);
void layec_write_warn(layec_context* context, layec_location location, const char* format, ...);
void layec_write_error(layec_context* context, layec_location location, const char* format, ...);
void layec_write_ice(layec_context* context, layec_location location, const char* format, ...);

string layec_context_intern_string_view(layec_context* context, string_view s);

#define LAYEC_ICE(C, L, F) do { layec_write_ice(C, L, F); exit(1); } while (0)
#define LAYEC_ICEV(C, L, F, ...) do { layec_write_ice(C, L, F, __VA_ARGS__); exit(1); } while (0)

// ========== Shared Data ==========

const char* layec_status_to_cstring(layec_status status);
const char* layec_value_category_to_cstring(layec_value_category category);

bool layec_evaluated_constant_equals(layec_evaluated_constant a, layec_evaluated_constant b);

layec_dependency_graph* layec_dependency_graph_create_in_context(layec_context* context);
void layec_dependency_graph_destroy(layec_dependency_graph* graph);
void layec_depgraph_add_dependency(layec_dependency_graph* graph, layec_dependency_entity* node, layec_dependency_entity* dependency);
void layec_depgraph_ensure_tracked(layec_dependency_graph* graph, layec_dependency_entity* node);
layec_dependency_order_result layec_dependency_graph_get_ordered_entities(layec_dependency_graph* graph);

int layec_get_significant_bits(int64_t value);

// ========== IR ==========

void layec_irpass_validate(layec_module* module);
string layec_codegen_llvm(layec_module* module);

// Module API

layec_module* layec_module_create(layec_context* context, string_view module_name);
void layec_module_destroy(layec_module* module);
layec_value* layec_module_create_function(layec_module* module, layec_location location, string_view function_name, layec_type* function_type, layec_linkage linkage);

layec_context* layec_module_context(layec_module* module);
string_view layec_module_name(layec_module* module);
int64_t layec_module_function_count(layec_module* module);
layec_value* layec_module_get_function_at_index(layec_module* module, int64_t function_index);

string layec_module_print(layec_module* module);

// Type API

const char* layec_type_kind_to_cstring(layec_type_kind kind);

layec_type_kind layec_type_get_kind(layec_type* type);

layec_type* layec_void_type(layec_context* context);
layec_type* layec_int_type(layec_context* context, int bit_width);
layec_type* layec_function_type(
    layec_context* context,
    layec_type* return_type,
    dynarr(layec_type*) parameter_types,
    layec_calling_convention calling_convention,
    bool is_variadic
);

bool layec_type_is_ptr(layec_type* type);
bool layec_type_is_void(layec_type* type);
bool layec_type_is_array(layec_type* type);
bool layec_type_is_function(layec_type* type);
bool layec_type_is_integer(layec_type* type);
bool layec_type_is_float(layec_type* type);
bool layec_type_is_struct(layec_type* type);

int layec_type_size_in_bits(layec_type* type);
int layec_type_size_in_bytes(layec_type* type);
int layec_type_align_in_bits(layec_type* type);
int layec_type_align_in_bytes(layec_type* type);

void layec_type_print_to_string(layec_type* type, string* s, bool use_color);

// - Function Type API

int64_t layec_function_type_parameter_count(layec_type* function_type);
layec_type* layec_function_type_get_parameter_type_at_index(layec_type* function_type, int64_t parameter_index);
bool layec_function_type_is_variadic(layec_type* function_type);

// Value API

const char* layec_value_kind_to_cstring(layec_value_kind kind);

int64_t layec_value_integer_constant(layec_value* value);

layec_value_kind layec_value_get_kind(layec_value* value);
layec_context* layec_value_context(layec_value* value);
layec_location layec_value_location(layec_value* value);
layec_type* layec_value_get_type(layec_value* value);
layec_type* layec_value_type(layec_value* value);
string_view layec_value_name(layec_value* value);
bool layec_value_is_terminating_instruction(layec_value* instruction);

bool layec_value_is_block(layec_value* value);
bool layec_value_is_function(layec_value* value);
bool layec_value_is_instruction(layec_value* value);

layec_value* layec_void_constant(layec_context* context);
layec_value* layec_int_constant(layec_context* context, layec_location location, layec_type* type, int64_t value);

void layec_value_print_to_string(layec_value* value, string* s, bool use_color);

// - Function Value API

string_view layec_function_name(layec_value* function);
layec_type* layec_function_return_type(layec_value* function);
int64_t layec_function_block_count(layec_value* function);
layec_value* layec_function_get_block_at_index(layec_value* function, int64_t block_index);
int64_t layec_function_parameter_count(layec_value* function);
layec_value* layec_function_get_parameter_at_index(layec_value* function, int64_t parameter_index);
bool layec_function_is_variadic(layec_value* function);

layec_value* layec_function_append_block(layec_value* function, string_view name);

// - Block API

bool layec_block_has_name(layec_value* block);
string_view layec_block_name(layec_value* block);
int64_t layec_block_index(layec_value* block);
int64_t layec_block_instruction_count(layec_value* block);
layec_value* layec_block_get_instruction_at_index(layec_value* block, int64_t instruction_index);
bool layec_block_is_terminated(layec_value* block);

// - Instruction API

layec_value* layec_instruction_callee(layec_value* call);
int64_t layec_instruction_call_argument_count(layec_value* call);
layec_value* layec_instruction_call_get_argument_at_index(layec_value* call, int64_t argument_index);

// Builder API

layec_builder* layec_builder_create(layec_context* context);
void layec_builder_destroy(layec_builder* builder);
layec_context* layec_builder_get_context(layec_builder* builder);

// Unsets this builder's insert position, so no further insertions will be allowed
// until another call to `layec_builder_position_*` is called.
void layec_builder_reset(layec_builder* builder);
void layec_builder_position_before(layec_builder* builder, layec_value* instruction);
void layec_builder_position_after(layec_builder* builder, layec_value* instruction);
void layec_builder_position_at_end(layec_builder* builder, layec_value* block);
layec_value* layec_builder_get_insert_block(layec_builder* builder);
void layec_builder_insert(layec_builder* builder, layec_value* instruction);
void layec_builder_insert_with_name(layec_builder* builder, layec_value* instruction, string_view name);

layec_value* layec_build_call(layec_builder* builder, layec_location location, layec_value* callee, layec_type* callee_type, dynarr(layec_value*) arguments, string_view name);
layec_value* layec_build_return(layec_builder* builder, layec_location location, layec_value* value);
layec_value* layec_build_return_void(layec_builder* builder, layec_location location);
layec_value* layec_build_unreachable(layec_builder* builder, layec_location location);

#endif // LAYEC_H
