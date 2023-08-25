#ifndef LAYEC_CONTEXT_H
#define LAYEC_CONTEXT_H

#include "layec/diag.h"
#include "layec/source.h"
#include "layec/vector.h"

typedef struct layec_context layec_context;

struct layec_context
{
    vector(layec_source_buffer) sources;
};

/// Create a layec_context.
layec_context* layec_context_create(void);
/// Destroy a layec_context.
void layec_context_destroy(layec_context* context);

/// Get the ID of a layec_source_buffer for the given file if one has already been created, otherwise
/// creates the layec_source_buffer and returns the new ID.
int layec_context_get_or_add_source_buffer_from_file(layec_context* context, const char* file_path);

/// Get the layec_source_buffer associated with the given source ID.
/// If an invlaid ID is given, an empty buffer is returned.
layec_source_buffer layec_context_get_source_buffer(layec_context* context, int source_id);

void layec_context_issue_diagnostic_prolog(layec_context* context, layec_diagnostic_severity severity, layec_location location);
void layec_context_issue_diagnostic_epilog(layec_context* context, layec_diagnostic_severity severity, layec_location location);

#endif // LAYEC_CONTEXT_H
