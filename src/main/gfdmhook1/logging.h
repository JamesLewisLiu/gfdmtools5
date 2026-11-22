#ifndef GFDMHOOK1_LOGGING_H
#define GFDMHOOK1_LOGGING_H
#include <stddef.h>
void gfdm_logging_init(void);
void gfdm_log_writer(void *context, const char *text, size_t length);
void gfdm_avs_log_writer_filtered(void *context, const char *text, size_t length);
#endif
