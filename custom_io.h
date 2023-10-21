#ifndef CUSTOM_IO_H
#define CUSTOM_IO_H

#include "definitions.h"


void log(const char *str);
void log(int value);
void logln(const char *str);
void logln(int value);
void throw_error(const char *error_text);
void logln_debug(const char *str);
void logln_debug(int value);
void log_debug(const char *str);
void log_debug(int value);

#endif