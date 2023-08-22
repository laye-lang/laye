#ifndef LAYEC_DIAG_H
#define LAYEC_DIAG_H

typedef enum layec_diagnostic_severity
{
    LAYEC_SEV_INFO,
    LAYEC_SEV_WARN,
    LAYEC_SEV_ERROR,
    LAYEC_SEV_FATAL,
    LAYEC_SEV_SORRY,
    LAYEC_SEV_COUNT,
} layec_diagnostic_severity;

#endif // LAYEC_DIAG_H
