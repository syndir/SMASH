/**
 * @file debug.h
 * @author Daniel Calabria
 *
 * Some helpful macros for printing debug and error information.
 **/

#ifndef DEBUG_H
#define DEBUG_H

/**
 * Debug information printing macro. Only functional if compiled with DEBUG
 * defined.
 **/
#ifdef DEBUG
#undef DEBUG
#define DEBUG(str, ...) \
    fprintf(stderr, "DEBUG [%s:%d]: " str "\n", \
            __FILE__, \
            __LINE__, \
            ##__VA_ARGS__)
#else
#define DEBUG(str, ...)
#endif

/**
 * ERROR macro -> prints an error message to stderr, regardless of debug level.
 **/
#define ERROR(str, ...) \
    fprintf(stderr, "ERROR: " str "\n", \
            ##__VA_ARGS__)

#endif // DEBUG_H
