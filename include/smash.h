/**
 * @file smash.h
 * @author Daniel Calabria
 *
 * Header file for smash.c
 **/

#ifndef SMASH_H
#define SMASH_H

/* Prompt */
#define PROMPT "smash> "

/* Macro to perform NULL checks on values (or other expressions
 * which also evaluate to 0) */
#define VALIDATE(arg, msg, rval, jmp) \
    { \
        if(!(arg)) \
        { \
            DEBUG("%s", (msg)); \
            retval = (rval); \
            goto jmp; \
        } \
    }

/* FREE macro -- if x is non-NULL, free() it, then set it to NULL */
#define FREE(x) \
    if((x)) \
    { \
        free((x)); \
        (x) = NULL; \
    }

/* exported variables */
extern unsigned int interactive;
extern unsigned int enable_debug;
extern unsigned int enable_rusage;
extern pid_t shell_pgid;
extern struct termios shell_termattr;

/* smash.c fxn prototypes */
int smash_setup(int argc, char *argv[]);
int smash_main();

#endif // SMASH_H
