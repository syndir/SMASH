/**
 * @file builtin.h
 * @author Daniel Calabria
 *
 * Header file for builtin.c
 **/

#ifndef BUILTIN_H
#define BUILTIN_H

#include "parse.h"

/**
 * Each builtin command can be a K/V pair where the key is the command string,
 * and the value is the callback to be invoked
 **/
typedef struct builtin_s
{
    char *command;
    int (*callback)(char *);
} builtin_t;

extern const builtin_t builtins[];

/* fxn prototypes for builtin.c */
int is_builtin(char *);

#endif // BUILTIN_H
