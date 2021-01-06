/**
 * @file builtin.c
 * @author Daniel Calabria
 *
 * Handles builtin commands for smash.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <ctype.h>
#ifdef EXTRA_CREDIT
#include <glob.h>
#endif

#include "smash.h"
#include "debug.h"
#include "builtin.h"
#include "jobs.h"

#define NUM_BUILTINS        8
#define BUILTIN_DELIMS      "\t\r\n "

static int builtin_exit(char *cmd);
static int builtin_cd(char *cmd);
static int builtin_pwd(char *cmd);
static int builtin_echo(char *cmd);
static int builtin_jobs(char *cmd);
static int builtin_fg(char *cmd);
static int builtin_bg(char *cmd);
static int builtin_kill(char *cmd);
static int builtin_comment(char *cmd);

/**
 * Here we define all builtin commands and their callbacks
 **/
const builtin_t builtins[] =
{
    /* exit */
    { "exit",       builtin_exit    },

    /* terminal control */
    { "cd",        builtin_cd       },
    { "pwd",        builtin_pwd     },

    /* echo */
    { "echo",      builtin_echo     },

    /* job control */
    { "jobs",       builtin_jobs    },
    { "fg",        builtin_fg       },
    { "bg",        builtin_bg       },
    { "kill",      builtin_kill     },

    /* comments */
    { "#",          builtin_comment }
};

/**
 * int builtin_exit(char *)
 *
 * @brief  Exits the shell, using any exit code provided as the param to exit().
 *
 * @param cmd  The command corresponding to "exit".
 *
 * @return  0 on success (but, if it succeeded, the program will exit before
 *          the return), or -errno on failure.
 **/
static int builtin_exit(char *cmd)
{
    DEBUG("builtin_exit() - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    VALIDATE(cmd && strlen(cmd) == 4 && strncmp(cmd, "exit", 4) == 0,
            "command must be \'exit\'",
            -EINVAL,
            builtin_exit_end);

    exit(0);

builtin_exit_end:
    DEBUG("builtin_exit() - EXIT [%d]", retval);
    return retval;
}

/**
 * int builtin_cd(char *)
 *
 * @brief  Changes the current working directory.
 *
 * @param cmd  The command corresponding to "cd <path>".
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_cd(char *cmd)
{
    DEBUG("builtin_cd() - ENTER [cmd @ %p]", cmd);
    int retval = 0;
    char *path = NULL;

    VALIDATE(cmd,
            "command must be non-NULL",
            -EINVAL,
            builtin_cd_end);

    VALIDATE(strncmp(cmd, "cd", 2) == 0,
            "command is not \'cd\'",
            -EINVAL,
            builtin_cd_end);

    char *p = cmd + 2;
    while(*p && isspace(*p))
        p++;

    if(!(*p))
    {
        /* if user just typed 'cd', go to $HOME */
       if((path = getenv("HOME")) == NULL)
       {
           retval = -EINVAL;
           ERROR("No set $HOME variable.");
           goto builtin_cd_end;
       }
    }
    else
    {
        path = p;
        if(path[0] == '$')
        {
            path = getenv(&path[1]);
            if(!path)
            {
                path = malloc(sizeof(char));
                if(!path)
                {
                    ERROR("malloc() failed: %s", strerror(errno));
                    retval = -1;
                    goto builtin_cd_end;
                }
                path[0] = 0;
            }
        }
    }

#ifdef EXTRA_CREDIT
    glob_t gbuf;
    int got_glob = 0;

    /* support for 'cd ~' */
    if(path[0] == '~')
    {
        if(glob(path, GLOB_TILDE, NULL, &gbuf) != 0)
        {
            ERROR("glob() failed to perform tilde expansion");
            retval = -1;
            goto builtin_cd_end;
        }

        path = gbuf.gl_pathv[0];
        got_glob = 1;
    }
#endif

    if(chdir(path) < 0)
    {
        retval = -errno;
        ERROR("%s", strerror(-retval));
        goto builtin_cd_end;
    }

    DEBUG("changed to directory: %s", path);

#ifdef EXTRA_CREDIT
    /* free glob memory allocated */
    if(got_glob)
        globfree(&gbuf);
#endif

builtin_cd_end:
    DEBUG("builtin_cd() - EXIT [%d]", retval);
    return retval;
}

/**
 * int builtin_pwd(char *)
 *
 * @brief  Outputs the current working directory of the shell.
 *
 * @param cmd  The command for "pwd"
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_pwd(char *cmd)
{
    DEBUG("builtin_pwd() - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    char *pwd = NULL;
    int size = PATH_MAX;

    VALIDATE(cmd && strncmp(cmd, "pwd", 3) == 0,
            "command must be \'pwd\'",
            -EINVAL,
            builtin_pwd_end);

builtin_pwd_try:
    if((pwd = malloc(sizeof(char) * size)) == NULL)
    {
        retval = -errno;
        ERROR("malloc() failed to allocate buffer for pathname: %s", strerror(errno));
        goto builtin_pwd_end;
    }

    if((pwd = getcwd(pwd, size-1)) == NULL)
    {
        if(errno == ENAMETOOLONG)
        {
            FREE(pwd);
            size = size << 1;
            goto builtin_pwd_try;
        }

        retval = -errno;
        ERROR("getcwd() failed: %s", strerror(errno));
        goto builtin_pwd_end;
    }

    dprintf(STDOUT_FILENO, "%s\n", pwd);

builtin_pwd_end:
    FREE(pwd);
    DEBUG("builtin_pwd() - EXIT [%d]", retval);
    return retval;
}

/**
 * int builtin_echo(char *)
 *
 * @brief  Echo command support. This will try to convert $VARS to their
 *         corresponding values (as read from environment variables).
 *
 * @param cmd  The command containing the information for echo.
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_echo(char *cmd)
{
    DEBUG("builtin_echo() - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    VALIDATE(cmd,
            "command must be non-NULL",
            -EINVAL,
            builtin_echo_end);

    /* basically, loop through each component of the command. if it starts with
     * a '$', look up the symbol using getenv().
     */
    char *c = cmd;
    while(*c && !isspace(*c))
        c++;

    char *tok = NULL, *saveptr = NULL;
    char *var = NULL;

    for(; ; c = NULL)
    {
        tok = strtok_r(c, BUILTIN_DELIMS, &saveptr);
        if(!tok)
            break;

        if(*tok == '$')
        {
            tok++;

            if(strcmp(tok, "?") == 0)
            {
                dprintf(STDOUT_FILENO, "%d", last_exit_code);
            }
            else
            {
                var = getenv(tok);
                dprintf(STDOUT_FILENO, "%s", (var ? var : ""));
            }
        }
        else
        {
            dprintf(STDOUT_FILENO, "%s", tok);
        }
        dprintf(STDOUT_FILENO, " ");
    }

    dprintf(STDOUT_FILENO, "\n");

builtin_echo_end:
    DEBUG("builtin_echo() - END [%d]", retval);
    return retval;
}

/**
 * int builtin_jobs(char *)
 *
 * @brief  Lists all current jobs
 *
 * @param cmd  (unused)
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_jobs(char *cmd)
{
    DEBUG("builtin_jobs() - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    jobs_list();

    DEBUG("builtin_jobs() - EXIT [%d]", retval);
    return retval;
}

/**
 * int builtin_fg(char *)
 *
 * @brief  Resumes a job in the foreground.
 *
 * @param cmd  The command entered, which will contain the job number
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_fg(char *cmd)
{
    DEBUG("builtin_fg - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    VALIDATE(cmd,
            "command must be non-NULL",
            -EINVAL,
            builtin_fg_end);

    if(strncmp(cmd, "fg ", 3) == 0)
    {
        cmd = cmd + 3;
        char *endp = NULL;
        DEBUG("fg target job id: %s", cmd);
        errno = 0;
        int id = strtol(cmd, &endp, 10);
        if(errno == EINVAL || cmd == endp)
            goto builtin_fg_fail;

        job_t *j = jobs_lookup_by_jobid(id);
        VALIDATE(j,
                "Invalid job id.",
                -EINVAL,
                builtin_fg_end);

        run_in_foreground(j, 1);
    }
    else
    {
builtin_fg_fail:
        dprintf(STDERR_FILENO, "Usage: fg [jobid]\n");
        retval = -EINVAL;
        goto builtin_fg_end;
    }

builtin_fg_end:
    DEBUG("builtin_fg() - END [%d]", retval);
    return retval;
}

/**
 * int builtin_bg(char *)
 *
 * @brief  Resumes a job in the background.
 *
 * @param cmd  The command entered, which will contain the job number
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_bg(char *cmd)
{
    DEBUG("builtin_bg() - ENTER [cmd=\'%s\']", cmd);
    int retval = 0;

    VALIDATE(cmd,
            "command must be non-NULL",
            -EINVAL,
            builtin_bg_end);

    if(strncmp(cmd, "bg ", 3) == 0)
    {
        cmd = cmd + 3;
        char *endp = NULL;
        DEBUG("bg target job id: %s", cmd);
        errno = 0;
        int id = strtol(cmd, &endp, 10);
        if(errno == EINVAL || cmd == endp)
            goto builtin_bg_fail;

        job_t *j = jobs_lookup_by_jobid(id);
        VALIDATE(j,
                "Invalid job id.",
                -EINVAL,
                builtin_bg_end);

        run_in_background(j, 1);
    }
    else
    {
builtin_bg_fail:
        dprintf(STDERR_FILENO, "Usage: bg [jobid]\n");
        retval = -EINVAL;
        goto builtin_bg_end;
    }

builtin_bg_end:
    DEBUG("builtin_bg() - EXIT [%d]", retval);
    return retval;
}

/**
 * int builtin_kill (char *)
 *
 * @brief  Send a signal to the specified process.
 *         kill should be entered as `kill -N jobid`.
 *
 * @param cmd  (unused)
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_kill(char *cmd)
{
    DEBUG("builtin_kill() - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    VALIDATE(cmd,
            "command must be non-NULL",
            -EINVAL,
            builtin_kill_end);

    if(strncmp(cmd, "kill ", 5) == 0)
    {
        cmd = cmd + 5;

        while(*cmd && isspace(*cmd))
            cmd++;

        if(*cmd != '-')
            goto builtin_kill_fail;

        cmd++;

        char *endp = NULL;
        errno = 0;

        /* signal # */
        int signum = strtol(cmd, &endp, 10);
        if(errno == EINVAL || cmd == endp)
            goto builtin_kill_fail;

        /* job id */
        cmd = endp;
        int jobid = strtol(cmd, &endp, 10);
        if(errno == EINVAL || cmd == endp)
            goto builtin_kill_fail;

        job_t *j = jobs_lookup_by_jobid(jobid);
        if(!j)
        {
            ERROR("No such job.");
            retval = -EINVAL;
            goto builtin_kill_end;
        }

        /* we can only run something in the foreground that hasn't already started
         * or is currently suspended */
        VALIDATE(j->status == SUSPENDED || j->status == RUNNING,
                "job is in incorrect state",
                -1,
                builtin_kill_fail);

        DEBUG("sending signum %d to job %d (pid %d)", signum, jobid, j->pgid);

        if(killpg(j->pgid, signum) < 0)
        {
            ERROR("Failed to send signal to job: %s", strerror(errno));
        }
    }
    else
    {
builtin_kill_fail:
        dprintf(STDERR_FILENO, "Usage: kill -N jobid\n");
        retval = -EINVAL;
        goto builtin_kill_end;
    }

builtin_kill_end:
    DEBUG("builtin_kill() - EXIT [%d]", retval);
    return retval;
}

/**
 * int builtin_comment(char *)
 *
 * @brief  Handles commands which are comments (start with "#").
 * @param cmd  The command which is a comment.
 *
 * @return  0 on success, -errno on failure
 **/
static int builtin_comment(char *cmd)
{
    DEBUG("builtin_comment() - ENTER [cmd @ %p]", cmd);
    int retval = 0;

    /* we don't actually need to do anything for a comment */

    DEBUG("builtin_comment() - EXIT [%d]", retval);
    return retval;
}

/**
 * int is_builtin(char *)
 *
 * @brief  Determines if the char* is a builtin command.
 *
 * @param cmd  The string to check.
 *
 * @return  The index of the builtin command in the builtins array, or -1 if not
 *          found.
 **/
int is_builtin(char *cmd)
{
    DEBUG("is_builtin() - ENTER [cmd=\'%s\']", cmd);
    int retval = -1;
    char *buf = NULL;

    VALIDATE(cmd,
            "command must be non-NULL",
            -1,
            is_builtin_end);

    buf = strdup(cmd);
    VALIDATE(buf,
            "strdup() failed to copy buffer",
            -1,
            is_builtin_end);

    /* extract first token */
    char *tok = NULL;
    tok = strtok(buf, BUILTIN_DELIMS);
    VALIDATE(tok,
            "token must be non-NULL",
            -1,
            is_builtin_end);

    for(int i = 0; i < NUM_BUILTINS; i++)
    {
        if((strncmp(tok, builtins[i].command, strlen(tok)) == 0) &&
           (strlen(tok) == strlen(builtins[i].command)))
        {
            DEBUG("found builtin command: %s", builtins[i].command);
            retval = i;
            goto is_builtin_end;
        }
    }


is_builtin_end:
    FREE(buf);
    DEBUG("is_builtin() - EXIT [%d]", retval);
    return retval;
}
