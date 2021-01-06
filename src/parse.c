/**
 * @file parse.c
 * @author Daniel Calabria
 *
 * Parses a string into it's comprised parts.
 *
 * A 'user_input' is of the form: `./a; ./b; ./c -t c`
 *
 * Given this as user input, it will be further decomposed into:
 * A `command` list which is of the form: `./a`, `./b`, and `./c -t c`
 *
 * Each command will be then be decomposed into it's individual delimited
 * components:
 * A `component` is of the form: `./a`, `./b`, and `./c` `-t` `c`
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "smash.h"
#include "debug.h"
#include "parse.h"

/**
 * void free_components(component_t *)
 *
 * @brief  Frees memory associated with components, which are stored as SLL.
 *         This will traverse the list, freeing the entries as it progresses.
 *
 * @param comp  The head of the list of components to free.
 **/
static void free_components(component_t *comp)
{
    if(comp)
    {
        component_t *n = comp->next;
        while(comp)
        {
            n = comp->next;
            FREE(comp->component);
            FREE(comp);
            comp = n;
        }
    }
}

/**
 * void free_commands(command_t *)
 *
 * @brief  Frees memory associated with commands, which are stored as SLL. This
 *         will traverse the list, freeing the entries as it progresses.
 *
 * @param cmd  The head of the list of commands to free.
 **/
static void free_commands(command_t *cmd)
{
    if(cmd)
    {
        command_t *n = cmd->next;
        while(cmd)
        {
            n = cmd->next;
            free_components(cmd->components);
            cmd->components = NULL;
            FREE(cmd->command);
            FREE(cmd->redirect_stdout);
            FREE(cmd->redirect_stderr);
            FREE(cmd->redirect_stdin);
            FREE(cmd);
            cmd = n;
        }
    }
}

/**
 * int free_input(user_input_t *)
 *
 * @brief Cleans up a user_input, freeing all nested structs as necessary.
 *
 * @param ui  Pointer to the user_input to clean up.
 * @return  0 on success, -errno on failure.
 **/
int free_input(user_input_t *ui)
{
    DEBUG("free_input() - ENTER (ui @ %p)", ui);
    int retval = 0;

    VALIDATE(ui,
            "can not clean a NULL user_input",
            -EINVAL,
            free_input_end);

    free_commands(ui->commands);
    ui->commands = NULL;
    FREE(ui->input);
    FREE(ui);

free_input_end:
    DEBUG("free_input() - EXIT [%d]", retval);
    return retval;
}

/**
 * int insert_component(component_t *, command_t *)
 *
 * @brief  Inserts the given component at the end of the component list stored
 *         in cmd.
 *
 * @param comp  The component to insert into cmd
 * @param cmd  The command_t which contains the list to append comp to.
 *
 * @return  0 on success, -errno on failure.
 **/
static int insert_component(component_t *comp, command_t *cmd)
{
    DEBUG("insert_component - ENTER [comp @ %p, cmd @ %p]", comp, cmd);
    int retval = 0;

    VALIDATE(comp,
            "comp must be non-NULL",
            -EINVAL,
            insert_component_end);
    VALIDATE(cmd,
            "cmd must be non-NULL",
            -EINVAL,
            insert_component_end);

    if(!cmd->components)
    {
        cmd->components = comp;
        goto insert_component_end;
    }

    component_t *c = cmd->components;
    while(c->next)
        c = c->next;
    c->next = comp;

insert_component_end:
    DEBUG("insert_component() - EXIT [%d]", retval);
    return retval;
}

/**
 * int insert_command(command_t *, user_input_t *)
 *
 * @brief  Inserts the given command at the end of the command list stored in
 *         ui.
 *
 * @param cmd  The command to append to the end of the list
 * @param ui  The user_input_t which contains the list to append cmd to
 *
 * @return  0 on success, -errno on failure
 **/
static int insert_command(command_t *cmd, user_input_t *ui)
{
    DEBUG("insert_command() - ENTER [cmd @ %p, ui @ %p]", cmd, ui);
    int retval = 0;

    VALIDATE(cmd,
            "cmd must be non-NULL",
            -EINVAL,
            insert_command_end);
    VALIDATE(ui,
            "ui must be non-NULL",
            -EINVAL,
            insert_command_end);

    if(!ui->commands)
    {
        ui->commands = cmd;
        goto insert_command_end;
    }

    command_t *c = ui->commands;
    while(c->next)
        c = c->next;
    c->next = cmd;

insert_command_end:
    DEBUG("insert_command() - EXIT [%d]", retval);
    return retval;
}

/**
 * user_input_t* parse_input(char *)
 *
 * @brief Parses the given string, splitting it into commands and components.
 *
 * @param input  The char* string to parse
 * @return  A pointer to a user_input_t representing the parsed input
 **/
user_input_t* parse_input(char *input)
{
    DEBUG("parse_input() - ENTER [input @ %p (\'%s\')]", input, input);
    user_input_t *retval = NULL;

    char *tok = NULL, *ctok = NULL;
    char *iptr = input, *cptr = NULL;
    char *saveiptr = NULL, *savecptr = NULL;

    VALIDATE(input,
            "can not parse a NULL input",
            NULL,
            parse_input_end);

    if((retval = malloc(sizeof(user_input_t))) == NULL)
    {
        ERROR("malloc() returned NULL: %s", strerror(errno));
        goto parse_input_fail;
    }
    memset(retval, 0, sizeof(user_input_t));

    /* save a copy of the original input */
    retval->input = strdup(input);

    /* first we break on `|` to separate commands */
    iptr = input;
    for(; ; iptr = NULL)
    {
        tok = strtok_r(iptr, COMMAND_DELIMS, &saveiptr);
        if(!tok)
        {
            DEBUG("no token found for COMMAND_DELIMS");
            goto parse_input_end;
        }
        DEBUG("i-token -> %s", tok);

        command_t *newc = malloc(sizeof(command_t));
        if(!newc)
        {
            ERROR("malloc() failed to allocate buffer for command_t");
            goto parse_input_fail;
        }
        memset(newc, 0, sizeof(command_t));
        newc->command = strdup(tok);
        insert_command(newc, retval);

        /* break commands into components (sep on whitespace) */
        for(cptr = tok; ; cptr = NULL)
        {
            ctok = strtok_r(cptr, COMPONENT_DELIMS, &savecptr);
            if(!ctok)
            {
                DEBUG("no token found for COMPONENT_DELIMS");
                goto parse_input_loop_end;
            }
            DEBUG("c-token -> %s", ctok);

            /* is it supposed to run in the background? */
            if(ctok[0] == '&')
            {
                /* if it begins with a &, that's it for this token */
                retval->is_background_command = 1;
                DEBUG("is background job");
                continue;
            }
            if(ctok[strlen(ctok)-1] == '&')
            {
                /* if it ends with a &, cut it out and see what the rest of the
                 * token is */
                retval->is_background_command = 1;
                DEBUG("is background job");
                ctok[strlen(ctok)-1] = 0;
            }

            /* redirection? */
            if(strncmp(ctok, ">>", 2) == 0)
            {
                if(strlen(ctok) > 2)
                {
                    /* was entered as `>>file` */
                    ctok += 2;
                }
                else
                {
                    /* was entered as `>> file` */
                    ctok = strtok_r(NULL, COMPONENT_DELIMS, &savecptr);
                }
                newc->redirect_stdout = strdup(ctok);
                newc->append_stdout = 1;

                DEBUG("redirecting stdout to append to: %s", newc->redirect_stdout);
                continue;
            }
            else if(strncmp(ctok, ">", 1) == 0)
            {
                if(strlen(ctok) > 1)
                {
                    /* was entered as `>file` */
                    ctok++;
                }
                else
                {
                    /* was entered as `> file` */
                    ctok = strtok_r(NULL, COMPONENT_DELIMS, &savecptr);
                }
                newc->redirect_stdout = strdup(ctok);

                DEBUG("redirecting stdout to: %s", newc->redirect_stdout);
                continue;
            }
            else if(strncmp(ctok, "2>", 2) == 0)
            {
                if(strlen(ctok) > 2)
                {
                    /* was entered as `2>file` */
                    ctok += 2;
                }
                else
                {
                    /* was entered as `2> file` */
                    ctok = strtok_r(NULL, COMPONENT_DELIMS, &savecptr);
                }
                newc->redirect_stderr = strdup(ctok);

                DEBUG("redirecting stderr to: %s", newc->redirect_stderr);
                continue;
            }
            else if(strncmp(ctok, "<", 1) == 0)
            {
                if(strlen(ctok) > 1)
                {
                    /* was entered as `<file` */
                    ctok++;
                }
                else
                {
                    /* was entered as `< file` */
                    ctok = strtok_r(NULL, COMPONENT_DELIMS, &savecptr);
                }
                newc->redirect_stdin = strdup(ctok);

                DEBUG("redirecting stdin to: %s", newc->redirect_stdin);
                continue;
            }

            component_t *newcomp = malloc(sizeof(component_t));
            if(!newcomp)
            {
                ERROR("malloc() failed to allocate buffer for component_t");
                goto parse_input_fail;
            }
            memset(newcomp, 0, sizeof(component_t));

            newcomp->component = strdup(ctok);
            insert_component(newcomp, newc);
        }
parse_input_loop_end:
        ;
    }

parse_input_fail:
    free_input(retval);

parse_input_end:
    DEBUG("parse_input() - EXIT [%p]", retval);
    return retval;
}

