/**
 * @file smash.c
 * @author Daniel Calabria
 *
 * A SMAll SHell.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#ifdef EXTRA_CREDIT
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include "smash.h"
#include "debug.h"
#include "io.h"
#include "parse.h"
#include "builtin.h"
#include "jobs.h"

unsigned int interactive = 1;
unsigned int enable_debug = 0;
unsigned int enable_rusage = 0;
pid_t shell_pgid;
struct termios shell_termattr;

static char *infile = NULL;

/**
 * void smash_atexit(void)
 *
 * @brief  atexit handler for smash. Cleans up used resources.
 **/
void smash_atexit(void)
{
    cancel_all_jobs();
    wait_for_all();
    FREE(infile);
    free_jobs();
}

/**
 * int usage(char *, char *)
 *
 * @brief  Prints a usage string to standard output.
 *
 * @param exec  The name of the executable
 * @param msg  Any other message to print
 *
 * @return  0 on success, -1 on failure
 **/
int usage(char *exec, char *msg)
{
    DEBUG("usage() - ENTER [exec @ %p, msg @ %p]", exec, msg);
    int retval = 0;

    VALIDATE(exec,
            "executable name must be non-NULL",
            -1,
            usage_end);

#ifdef EXTRA_CREDIT
    char *s = "[-d] [-t] [file]";
#else
    char *s = "[-d] [file]";
#endif

    int res = dprintf(STDOUT_FILENO, "%sUsage: %s %s\n",
            (msg ? msg : ""), exec, s);
    if(res < 0)
        retval = -1;

usage_end:
    DEBUG("usage() - EXIT [%d]", retval);
    return retval;
}

/**
 * int smash_setup(int, char *[])
 *
 * @brief  Performs setup for the smash shell. This includes setting up the
 *         signal handler, and parsing the command line. This function will check
 *         to see if we are supposed to load from a file given at the
 *         command line, or if we should be in interactive mode (read input from
 *         keyboard). If needed, will adjust the necessary file descriptors
 *         so that we still deal with stdin (fd 0) even when reading from
 *         a file.
 *
 * @param argc  The number of arguments
 * @param argv  The array of strings denoting the command line
 *
 * @return  0 on success, -errno on failure
 **/
int smash_setup(int argc, char *argv[])
{
    DEBUG("smash_setup() - ENTER [argc=%d, *argv[] @ %p]", argc, argv);
    int retval = 0;

    /* install atexit callback */
    if(atexit(smash_atexit) != 0)
    {
        ERROR("atexit() failed to install exit handler \'smash_atexit\'");
        retval = -1;
        goto smash_setup_end;
    }

    /* parse command line */
    int opt;
#ifdef EXTRA_CREDIT
    while((opt = getopt(argc, argv, "dt")) != -1)
#else
    while((opt = getopt(argc, argv, "d")) != -1)
#endif
    {
        switch(opt)
        {
            case 'd':
                enable_debug++;
                break;
#ifdef EXTRA_CREDIT
            case 't':
                enable_rusage++;
                break;
#endif
            default:
                usage(argv[0], NULL);
                exit(EXIT_FAILURE);
        }
    }

    /* given a file for batch mode? */
    if(argc > optind)
    {
        interactive = 0;
        infile = strdup(argv[optind]);
        VALIDATE(infile,
                "strdup() failed to copy string",
                -errno,
                smash_setup_end);

        /* redirect input for reading from a file */
        int in_fd = open(infile, O_RDONLY, 0);
        if(in_fd < 0)
        {
            ERROR("open() failed to open file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(dup2(in_fd, STDIN_FILENO) < 0)
        {
            ERROR("dup2() failed to copy file descriptor: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(close(in_fd) < 0)
        {
            ERROR("close() failed to close file descriptor: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if(interactive)
    {
        /* put shell in fg.. if the group controlling the terminal isnt the
         * group for our shell, kill -TTIN in  */
        while(tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        /* set job control signals to be ignored */
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sa.sa_flags = SA_RESTART;
        if(sigemptyset(&sa.sa_mask) < 0)
        {
            ERROR("sigemptyset() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(sigaction(SIGINT, &sa, NULL) == -1)
        {
            ERROR("sigaction() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(sigaction(SIGQUIT, &sa, NULL) == -1)
        {
            ERROR("sigaction() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(sigaction(SIGTSTP, &sa, NULL) == -1)
        {
            ERROR("sigaction() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(sigaction(SIGTTIN, &sa, NULL) == -1)
        {
            ERROR("sigaction() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(sigaction(SIGTTOU, &sa, NULL) == -1)
        {
            ERROR("sigaction() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        /* set up our shell process information and terminal control/attrs */
        shell_pgid = getpid();
        if(setpgid(shell_pgid, shell_pgid) < 0)
        {
            ERROR("setpgid() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(tcsetpgrp(STDIN_FILENO, shell_pgid) < 0)
        {
            ERROR("tcsetpgrp() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(tcgetattr(STDIN_FILENO, &shell_termattr) < 0)
        {
            ERROR("tcgetattr() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

smash_setup_end:
    DEBUG("smash_setup() - EXIT [%d]", retval);
    return retval;
}

/**
 * int smash_wait_all()
 *
 * @brief  Waits (non-blocking) on any finished children.
 *
 * @return  0 if no error occured, -1 on error
 **/
static int smash_wait_all()
{
    DEBUG("smash_wait_all() - ENTER");
    int retval = 0;

    pid_t pid;
    int status;

#ifdef EXTRA_CREDIT
    struct rusage r;
    while((pid = wait4(-1, &status, WNOHANG | WUNTRACED | WCONTINUED, &r)) > 0)
#else
    while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
#endif
    {
        DEBUG("waitpid for %d", pid);

        /* find job w/ matching pid */
        job_t *j = jobs;
        while(j)
        {
            if(j->pgid == pid)
                break;
            j = j->next;
        }

        VALIDATE(j,
                "waitpid() returned a pid w/ no job_t associated with it",
                -1,
                smash_wait_all_end);

        job_update_status(j, status);

#ifdef EXTRA_CREDIT
        if(enable_rusage)
        {
            struct timeval endtime, res;
            if(gettimeofday(&endtime, NULL) < 0)
            {
                /* even if we failed getting the time, we still want to clean
                 * up the job properly */
                ERROR("gettimeofday() failed: %s", strerror(errno));
                retval = -1;
            }

            /* how long did the job run for in real time? */
            timersub(&endtime, &j->starttime, &res);

            if(j->status == EXITED || j->status == ABORTED)
            {
                dprintf(STDERR_FILENO,
                        "TIMES: real=%ld.%1lds user=%ld.%1lds sys=%ld.%1lds\n",
                        res.tv_sec, res.tv_usec,
                        r.ru_utime.tv_sec, r.ru_utime.tv_usec,
                        r.ru_stime.tv_sec, r.ru_stime.tv_usec);
            }
        }
#endif
    }

smash_wait_all_end:
    DEBUG("smash_wait_all() - EXIT [%d]", retval);
    return retval;
}

/**
 * int smash_main()
 *
 * @brief  Main loop for smash shell. Drives reading of input, processing that
 *         input, checking against builtins, and executing processes.
 *
 * @return  0 on success, -1 on error
 **/
int smash_main()
{
    DEBUG("smash_main() - ENTER");
    int retval = 0;
    char *buf = NULL;

    while(1)
    {
        FREE(buf);

        /* wait on any processes which just finished */
        smash_wait_all();

        /* print prompt */
        if(interactive)
            io_print_prompt(PROMPT);

        /* read line */
        errno = 0;
        if((buf = io_readline()) == NULL)
        {
            retval = errno;
            /* ERROR("smash_readline() failed."); */
            goto smash_main_end;
        }
        DEBUG("read input: \'%s\'", buf);

        /* wait again in case anything finished after we got our input */
        smash_wait_all();

        /* trim any surrounding whitespace from the input */
        char *trimmed_buf = buf;
        char *endp = buf + strlen(buf) - 1;
        if(strlen(buf) < 1)
            endp = buf;
        while(*endp && endp != buf && isspace(*endp))
        {
            *endp = '\0';
            endp--;
        }
        while(*trimmed_buf && isspace(*trimmed_buf))
            trimmed_buf++;

        /* check for comment character (#) */
        char *comment = strchr(trimmed_buf, '#');
        if(comment)
            *comment = '\0';

        /* did we actually end up removing the entire command? */
        if(strlen(trimmed_buf) == 0)
            continue;

        /* check for builtins */
        int pos = -1;
        if((pos = is_builtin(trimmed_buf)) >= 0)
        {
            builtins[pos].callback(trimmed_buf);
            continue;
        }

        /* parse input */
        user_input_t *ui = parse_input(trimmed_buf);
        if(ui == NULL)
        {
            DEBUG("no input");
            continue;
        }

        /* execute the job */
        job_t *j = jobs_create(ui);
        VALIDATE(j,
                "failed to create job",
                -1,
                smash_main_end);

        if(exec_job(j) < 0)
        {
            DEBUG("failed to execute job");
            continue;
        }
    }

smash_main_end:
    FREE(buf);
    DEBUG("smash_main() - EXIT [%d]", retval);
    return retval;
}
