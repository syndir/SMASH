/**
 * @file jobs.c
 * @author Daniel Calabria
 *
 * Job handling for smash.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef EXTRA_CREDIT
#include <glob.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include "smash.h"
#include "debug.h"
#include "jobs.h"
#include "parse.h"
#include "builtin.h"

extern char **environ;
job_t *jobs = NULL;
int last_exit_code = 0;

int restore_shell_control(struct termios *termattr);

/**
 * void launch_child(command_t *, int )
 *
 * @brief  Actually launches a child process.
 *
 * @param cmd  The command to launch
 * @param pgid  The process group id this should be bound to
 * @param foreground  1 if this should be run in the foreground, 0 if background
 *
 * @return  This function never returns.
 **/
static void launch_child(command_t *cmd, int pgid, int foreground)
{
    DEBUG("launch_child() - ENTER [cmd @ %p]", cmd);

    pid_t pid;

    if(!cmd)
    {
        ERROR("cmd must be non-NULL!");
        exit(EXIT_FAILURE);
    }

    if(interactive)
    {
        pid = getpid();
        if(pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);

        /* do we need to give the process control of the terminal? */
        if(foreground)
        {
            if(tcsetpgrp(STDIN_FILENO, pgid) < 0)
            {
                DEBUG("pid=%d pgid=%d\n", pid, pgid);
                ERROR("tcsetpgrp() failed to set foreground process: %s", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        /* unignore the signals so that the child can deal with them */
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
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
        if(sigaction(SIGCHLD, &sa, NULL) == -1)
        {
            ERROR("sigaction() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    /* do we need to do any redirection? */
    if(cmd->redirect_stdout)
    {
        int new_stdout = -1;

        if(cmd->append_stdout)
        {
            new_stdout = open(cmd->redirect_stdout, O_APPEND | O_WRONLY,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

            if(new_stdout < 0)
            {
                /* if opening failed because the file doesn't exit, we should
                 * make it */
                if(errno == ENOENT)
                    goto launch_child_create_outfile;
                else
                {
                    ERROR("open() failed: %s", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
        }
        else
        {
launch_child_create_outfile:
            new_stdout = creat(cmd->redirect_stdout,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        }

        if(new_stdout < 0)
        {
            ERROR("open() failed to open file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(dup2(new_stdout, STDOUT_FILENO) < 0)
        {
            ERROR("dup2() failed to clone file descriptor: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(close(new_stdout) < 0)
        {
            ERROR("close() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    if(cmd->redirect_stderr)
    {
        int new_stderr = creat(cmd->redirect_stderr,
           S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

        if(new_stderr < 0)
        {
            ERROR("open() failed to open file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(dup2(new_stderr, STDERR_FILENO) < 0)
        {
            ERROR("dup2() failed to clone file descriptor: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(close(new_stderr) < 0)
        {
            ERROR("close() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    if(cmd->redirect_stdin)
    {
        int new_stdin = open(cmd->redirect_stdin, O_RDONLY, NULL);
        if(new_stdin < 0)
        {
            ERROR("open() failed to open file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(dup2(new_stdin, STDIN_FILENO) < 0)
        {
            ERROR("dup2() failed to clone file descriptor: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(close(new_stdin) < 0)
        {
            ERROR("close() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

#ifdef EXTRA_CREDIT
    /* in case of pipes, which take precedence over normal redirection */
    if(cmd->in_fd >= 0)
    {
        if(dup2(cmd->in_fd, STDIN_FILENO) < 0)
        {
            ERROR("dup2() failed to clone file descriptor: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(close(cmd->in_fd) < 0)
        {
            ERROR("close() failed; %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        cmd->in_fd = -1;
    }

    if(cmd->out_fd >= 0)
    {
        if(dup2(cmd->out_fd, STDOUT_FILENO) < 0)
        {
            ERROR("dup2() failed to clone file descriptior: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(close(cmd->out_fd) < 0)
        {
            ERROR("close() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        cmd->out_fd = -1;
    }
#endif

    /* build argv array */
    int argc = 0;
    char **argv = NULL;
    char *executable = NULL;

#ifdef EXTRA_CREDIT
    int num_nonglob = 0;
    int num_glob = 0;
#endif

    /* count number of arguments */
    component_t *c = cmd->components;
    while(c)
    {
        argc++;

#ifdef EXTRA_CREDIT
        /* count how many non-wildcard args there are, so we know how many slots
         * to preserve */
        if(c->component[0] == '*' || c->component[0] == '~')
            num_glob++;
        else
            num_nonglob++;
#endif
        c = c->next;
    }

    argv = malloc(sizeof(char *) * (argc+1));
    if(!argv)
    {
        ERROR("malloc() failed to allocate memory for argv array");
        exit(EXIT_FAILURE);
    }
    memset(argv, 0, sizeof(char *) * (argc+1));

    c = cmd->components;
    argv[0] = c->component;

#ifdef EXTRA_CREDIT
    glob_t gbuf;
    memset(&gbuf, 0, sizeof(glob_t));

    gbuf.gl_offs = num_nonglob;

    /* argv[0] is program itself */
    /* gbuf.gl_pathv[0] = c->component; */
#endif

    c = c->next;

#ifdef EXTRA_CREDIT
    for(int i = 1, done_glob = 0; i < argc; i++, c = c->next)
#else
    for(int i = 1; i < argc; i++, c = c->next)
#endif
    {
        DEBUG("comp is %s", c->component);

        /* is it a environment variable we need to handle? */
        if(c->component[0] == '$')
        {
            if(c->component[1] == '?')
            {
                argv[i] = malloc(sizeof(char) * 8);
                if(!argv[i])
                {
                    ERROR("malloc() failed: %s", strerror(errno));
                    exit(EXIT_FAILURE);
                }

                if(sprintf(argv[i], "%d", last_exit_code) < 0)
                {
                    ERROR("sprintf() failed");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                argv[i] = getenv(&c->component[1]);

                /* was the environment variable not found? */
                if(!argv[i])
                {
                    argv[i] = malloc(sizeof(char) * 1);
                    if(!argv[i])
                    {
                        ERROR("malloc() failed: %s", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                    argv[i][0] = 0;
                }
            }
        }
        else
        {
            argv[i] = c->component;
        }

#ifdef EXTRA_CREDIT
        /* support glob'ing */
        /* is the token a wildcard? */
        if(argv[i][0] == '*' || argv[i][0] == '~')
        /* if(strncmp(argv[i], "*", 1) == 0) */
        {
            DEBUG("wildcard token: %s", argv[i]);
            int flags = GLOB_DOOFFS | GLOB_TILDE;
            if(done_glob)
                flags |= GLOB_APPEND;

            if(glob(argv[i], flags, NULL, &gbuf) != 0)
            {
                /* no matches, or some error */
                ERROR("no matches found: %s", argv[i]);
                exit(EXIT_FAILURE);
            }
            done_glob = 1;
        }
        else
        {
            /* don't add wildcards to the gl_pathv array until after
             * we build all the rest of the array using glob(3) */
            DEBUG("non-wildcard: %s", argv[i]);
        }
#endif
    }
    executable = argv[0];

#ifdef EXTRA_CREDIT
    if(num_glob > 0)
    {
        gbuf.gl_pathv[0] = executable;
        c = cmd->components;
        for(int i = 0, cur = 0; i < argc; i++)
        {
            if(argv[i][0] != '*' && argv[i][0] != '~')
            {
                DEBUG("got non wildcard");
                gbuf.gl_pathv[cur++] = argv[i];
            }
        }
    }
#endif

    /* execute file */
    if(enable_debug)
    {
        dprintf(STDERR_FILENO, "RUNNING: %s\n", cmd->command);
    }

    /* execvp searches through PATHs so we don't have to */
#ifdef EXTRA_CREDIT
    if(num_glob > 0)
    {
        if(execvp(executable, &gbuf.gl_pathv[0]) == -1)
        {
            ERROR("%s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
#endif

    if(execvp(executable, argv) == -1)
    {
        ERROR("%s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* shouldn't get here?? */
    exit(EXIT_FAILURE);
}

/**
 * int run_in_background(job_t *)
 *
 * @brief  Sends a SIGCONT signal to the process, to ensure that it's running.
 *
 * @param job  The job to run in the background
 * @param cont  1 if we should send a SIGCONT to the process, 0 otherwise
 *
 * @return  0 on success, -1 on failure
 **/
int run_in_background(job_t *job, int cont)
{
    DEBUG("run_in_background() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            run_in_background_end);

    /* we can only run something in the foreground that hasn't already started
     * or is currently suspended */
    VALIDATE(job->status == NEW || job->status == SUSPENDED,
            "job is in incorrect state",
            -1,
            run_in_background_end);

    job->status = RUNNING;
    job->is_in_bg = 1;

    if(cont)
    {
        if(killpg(job->pgid, SIGCONT) < 0)
        {
            ERROR("killpg() failed to send SIGCONT to child: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

run_in_background_end:
    DEBUG("run_in_background - EXIT [%d]", retval);
    return retval;
}

/**
 * int job_wait(job_t *)
 *
 * @brief  Waits for a job to complete.
 *
 * @param job  The job to wait for.
 *
 * @return  0 on success, -1 on error
 **/
int job_wait(job_t *job)
{
    DEBUG("job_wait() - ENTER [job @ %p]", job);
    int retval = 0;

    pid_t pid;
    int status;

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            job_wait_end);

#ifdef EXTRA_CREDIT
    if(enable_rusage)
    {
        struct rusage r;
        if((pid = wait4(job->pgid, &status, WUNTRACED, &r)) > 0)
        {
            DEBUG("REAPED %d", pid);
            struct timeval endtime, res;
            if(gettimeofday(&endtime, NULL) < 0)
            {
                /* even if we failed getting the time, we still want to clean
                 * up the job properly */
                ERROR("gettimeofday() failed: %s", strerror(errno));
                retval = -1;
            }

            /* how long did the job run for in real time? */
            timersub(&endtime, &job->starttime, &res);

            job_update_status(job, status);

            if(job->status == EXITED || job->status == ABORTED)
            {
                dprintf(STDERR_FILENO,
                        "TIMES: real=%ld.%1lds user=%ld.%1lds sys=%ld.%1lds\n",
                        res.tv_sec, res.tv_usec,
                        r.ru_utime.tv_sec, r.ru_utime.tv_usec,
                        r.ru_stime.tv_sec, r.ru_stime.tv_usec);
            }
        }
        goto job_wait_end;
    }
#endif

    if((pid = waitpid(job->pgid, &status, WUNTRACED)) > 0)
    {
        DEBUG("REAPED %d", pid);
        job_update_status(job, status);
    }

job_wait_end:
    DEBUG("job_wait() - EXIT [%d]", retval);
    return retval;
}

/**
 * int restore_shell_control()
 *
 * @brief  Returns the control of the terminal to the shell
 *
 * @return  0 on success, -1 on failure.
 **/
int restore_shell_control(struct termios *termattr)
{
    DEBUG("restore_shell_control() - ENTER");
    int retval = 0;

    VALIDATE(termattr,
            "terminal attributes must be non-NULL",
            -1,
            restore_shell_control_end);

    if(tcsetpgrp(STDIN_FILENO, shell_pgid) < 0)
    {
        ERROR("tcsetpgrp() failed to reclaim control of terminal: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* restore attributes */
    if(tcgetattr(STDIN_FILENO, termattr) < 0)
    {
        ERROR("tcgetattr() failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_termattr) < 0)
    {
        ERROR("tcsetattr() failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

restore_shell_control_end:
    DEBUG("restore_shell_control() - EXIT");
    return retval;
}

/**
 * int run_in_foreground(job_t *)
 *
 * @brief  Switches control of the terminal to the child process
 *
 * @param job  The job to place in the foreground
 * @param cont  1 if we should send a SIGCONT to the child, 0 if not
 *
 * @return  0 on success, -1 on failure
 **/
int run_in_foreground(job_t *job, int cont)
{
    DEBUG("run_in_foreground() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            run_in_foreground_end);

    /* we can only run something in the foreground that hasn't already started
     * or is currently suspended */
    VALIDATE(job->status == NEW || job->status == SUSPENDED || job->status == RUNNING,
            "job is in incorrect state",
            -1,
            run_in_foreground_end);

    if(job->status == RUNNING && !job->is_in_bg)
    {
        DEBUG("job is already in fg");
        retval = -1;
        goto run_in_foreground_end;
    }

    int orig_status = job->status;
    job->status = RUNNING;
    job->is_in_bg = 0;

    /* give child control of the terminal */
    if(tcsetpgrp(STDIN_FILENO, job->pgid) < 0)
    {
        ERROR("tcsetpgrp() failed to bring job to foreground: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if(cont && orig_status != RUNNING)
    {
        /* save terminal attributes */
        if(tcsetattr(STDIN_FILENO, TCSADRAIN, &job->termattr) < 0)
        {
            ERROR("tcsetattr() failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(killpg(job->pgid, SIGCONT) < 0)
        {
            ERROR("killpg() failed to send SIGCONT to child: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    /* wait */
    job_wait(job);

    /* switch back to shell */
    restore_shell_control(&job->termattr);

    if(job->status == EXITED)
        last_exit_code = job->exitcode;
    else if(job->status == SUSPENDED)
        print_job(job);

run_in_foreground_end:
    DEBUG("run_in_foreground() - END [%d]", retval);
    return retval;
}

/**
 * int exec_job(job_t *)
 *
 * @brief  Begins execution of a particular job.
 *
 * @param job  The job to begin execution of
 *
 * @return  0 on success, -1 on failure
 **/
int exec_job(job_t *job)
{
    DEBUG("exec_job() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            exec_job_end);

    if(jobs_insert(job) < 0)
    {
        ERROR("failed to insert job into joblis");
        exit(EXIT_FAILURE);
    }

    command_t *cmd = job->ui->commands;
    VALIDATE(cmd,
            "command is not valid",
            -1,
            exec_job_end);

#ifdef EXTRA_CREDIT
    int fds[2];
    int in_fd = -1;

    /* save the first pgid so that, if pipelining, we can set the rest of the
     * children to also be members of that same pipeline */
    pid_t pgid = -1;

    while(cmd)
    {
        /* set the in/out fds. if the last command was part of redirection
         * pipeline, then in_fd will be valid, otherwise it will be -1. */
        cmd->in_fd = in_fd;
        cmd->out_fd = -1;

        /* support for pipes */
        if(cmd->next)
        {
            if(pipe(fds) < 0)
            {
                ERROR("pipe() failed: %s", strerror(errno));
                exit(EXIT_FAILURE);
            }

            cmd->out_fd = fds[1];
        }
#endif
        pid_t ppid = fork();
#ifdef EXTRA_CREDIT
        if(pgid < 0)
            pgid = ppid;
#endif
        if(ppid < 0)
        {
            ERROR("fork() failed to spawn child process: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(ppid == 0)
        {
            /* child */
#ifdef EXTRA_CREDIT
            job->pgid = pgid;
#else
            job->pgid = ppid;
#endif
            launch_child(cmd, job->pgid, !job->ui->is_background_command);
        }
        else
        {
#ifdef EXTRA_CREDIT
            job->pgid = pgid;
            if(interactive)
            {
                setpgid(ppid, pgid);
            }
#else
            /* parent */
            job->pgid = ppid;

            if(interactive)
            {
                setpgid(ppid, ppid);
            }
#endif
        }

#ifdef EXTRA_CREDIT
        /* clean up pipes, if necessary */
        if(cmd->in_fd >= 0)
        {
            if(close(cmd->in_fd) < 0)
            {
                ERROR("close() failed: %s", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
        if(cmd->out_fd >= 0)
        {
            if(close(cmd->out_fd) < 0)
            {
                ERROR("close() failed; %s", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        /* save this so the next cmd knows where to read from */
        in_fd = fds[0];

        cmd = cmd->next;
    }
#endif

    if(!interactive)
    {
        /* wait */
        job_wait(job);
        last_exit_code = job->exitcode;
    }
    else if(job->ui->is_background_command)
    {
        run_in_background(job, 0);
    }
    else
    {
        run_in_foreground(job, 0);
    }

exec_job_end:
    DEBUG("exec_job() - EXIT [%d]", retval);
    return retval;
}

/**
 * int job_update_status(job_t *, int)
 *
 * @brief  Updates the status field of the job
 *
 * @param j  The job to update
 * @param status  The status code
 *
 * @return  0 on success, -errno on failure
 **/
int job_update_status(job_t *j, int status)
{
    DEBUG("job_update_status() - ENTER [job @ %p, status=%d]", j, status);
    int retval = 0;

    VALIDATE(j,
            "job must be non-NULL",
            -EINVAL,
            job_update_status_end);

    if(WIFSTOPPED(status))
    {
        /* stopped/suspended */
        j->status = SUSPENDED;
    }
    else if(WIFCONTINUED(status))
    {
        /* continued */
        j->status = RUNNING;
    }
    else if(WIFSIGNALED(status))
    {
        /* killed/aborted */
        j->status = ABORTED;
        j->exitcode = WTERMSIG(status);
        if(enable_debug)
        {
            if(dprintf(STDERR_FILENO, "ABORTED: \'%s\' <signal=%d>\n",
                        j->ui->input, j->exitcode) < 0)
            {
                ERROR("dprintf() failed");
            }
        }
    }
    else if(WIFEXITED(status))
    {
        /* exited */
        j->status = EXITED;
        j->exitcode = WEXITSTATUS(status);
        if(enable_debug)
        {
            if(dprintf(STDERR_FILENO, "ENDED: \'%s\' <ret=%d>\n",
                        j->ui->input, j->exitcode) < 0)
            {
                ERROR("dprintf() failed");
            }
        }
    }

job_update_status_end:
    DEBUG("job_update_status() - EXIT [%d]", retval);
    return retval;
}

/**
 * int free_job(job_t *)
 *
 * @brief  Frees all memory associated with the corresponding job structure.
 *
 * @param job  The job to free
 *
 * @return  0 on success, -errno on failure
 **/
int free_job(job_t *job)
{
    DEBUG("free_job() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job is already NULL",
            -EINVAL,
            free_job_end);

    free_input(job->ui);
    FREE(job);

free_job_end:
    DEBUG("free_job() - EXIT [%d]", retval);
    return retval;
}

/**
 * void free_jobs()
 *
 * @brief Frees all jobs in the joblist.
 **/
void free_jobs()
{
    job_t *j = jobs, *jn = NULL;

    while(j)
    {
        jn = j->next;
        free_job(j);
        j = jn;
    }

    jobs = NULL;
}

/**
 * job_t* jobs_create(user_input_t *)
 *
 * @brief  Creates a new job structure, storing the user_input_t* inside of it.
 *
 * @param ui  Pointer to the user_input to store within the created job.
 *
 * @return  A pointer to the newly created job_t, or NULL on failure.
 **/
job_t* jobs_create(user_input_t *ui)
{
    DEBUG("jobs_create() - ENTER [ui @ %p]", ui);
    job_t *retval = NULL;

    VALIDATE(ui,
            "user_input_t must be non-NULL",
            NULL,
            jobs_create_end);

    retval = malloc(sizeof(job_t));
    VALIDATE(retval,
            "malloc() failed to allocate new job_t",
            NULL,
            jobs_create_end);

    /* clear it, instead of setting everything to 0 by hand */
    memset(retval, 0, sizeof(job_t));

    retval->ui = ui;

jobs_create_end:
    DEBUG("jobs_create() - EXIT [%p]", retval);
    return retval;
}

/**
 * int jobs_insert(job_t *)
 *
 * @brief  Inserts a job into the joblist, and updates the job's jobid to be
 *         1 higher than the previous node's jobid (or 1, for the first job).
 *
 * @param job  The job to insert
 *
 * @return  0 on success, -errno on failure
 **/
int jobs_insert(job_t *job)
{
    DEBUG("jobs_insert() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job must be non-NULL",
            -EINVAL,
            jobs_insert_end);

    job->next = NULL;

#ifdef EXTRA_CREDIT
    /* save start time information */
    if(gettimeofday(&job->starttime, NULL) < 0)
    {
        ERROR("gettimeofday() failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
#endif

    /* empty list? */
    if(jobs == NULL)
    {
        job->jobid = 1;
        job->next = jobs;
        jobs = job;
        goto jobs_insert_end;
    }

    /* list has contents, insert at the end, and set the jobid to be 1 more than
     * the last node's jobid */
    job_t *j = jobs;
    while(j->next)
        j = j->next;

    job->jobid = j->jobid+1;
    job->next = j->next;
    j->next = job;

jobs_insert_end:
    DEBUG("jobs_insert() - EXIT [%d]", retval);
    return retval;
}

/**
 * int jobs_remove(job_t *)
 *
 * @brief  Remove the specified job from the joblist.
 *
 * @param job  The job to remove
 *
 * @return  0 on success, -errno on failure
 **/
int jobs_remove(job_t *job)
{
    DEBUG("jobs_remove() - ENTER [job @ %p]", job);
    int retval = -EINVAL;

    job_t *j = jobs;
    job_t *jp = NULL;

    VALIDATE(job,
            "can not remove a NULL job",
            -EINVAL,
            jobs_remove_end);

    VALIDATE(jobs,
            "jobs list is currently empty",
            -EINVAL,
            jobs_remove_end);

    /* head of list? */
    if(j == job)
    {
        jobs = jobs->next;
        free_job(j);
        retval = 0;
        goto jobs_remove_end;
    }

    while(j)
    {
        if(j == job)
        {
            jp->next = j->next;
            free_job(j);
            retval = 0;
            goto jobs_remove_end;
        }

        jp = j;
        j = j->next;
    }

    ERROR("No such job found.\n");

jobs_remove_end:
    DEBUG("jobs_remove() - EXIT [%d]", retval);
    return retval;
}

/**
 * job_t* jobs_lookup_by_jobid(int)
 *
 * @brief  Finds and returns a pointer to the job with the specified jobid.
 *
 * @param jobid  The jobid to find
 *
 * @return  A pointer to the job_t structure with the corresponding jobid, or
 *          NULL if not found or on error.
 **/
job_t* jobs_lookup_by_jobid(int jobid)
{
    DEBUG("jobs_lookup_by_jobid() - ENTER [jobid=%d]", jobid);
    job_t *retval = jobs;

    while(retval)
    {
        if(retval->jobid == jobid)
            break;
        retval = retval->next;
    }

    DEBUG("jobs_lookup_by_jobid() - EXIT [%p]", retval);
    return retval;
}

/**
 * int cancel_all_jobs()
 *
 * @brief  Cancels all jobs currently running.
 *
 * @return  0 on success, -errno on error
 **/
int cancel_all_jobs()
{
    DEBUG("cancel_all_jobs() - ENTER");
    int retval = 0;

    job_t *j = jobs;
    while(j)
    {
        if(j->status == RUNNING || j->status == SUSPENDED)
        {
            /* send a CONT, then a TERM */
            if(killpg(j->pgid, SIGCONT) < 0 || killpg(j->pgid, SIGTERM) < 0)
            {
                retval = -errno;
                ERROR("killpg() failed: %s", strerror(errno));
                /* goto cancel_all_jobs_end; */
            }
            j->status = CANCELED;
        }

        j = j->next;
    }

/* cancel_all_jobs_end: */
    DEBUG("cancel_all_jobs() - EXIT [%d]", retval);
    return retval;
}

/**
 * int wait_for_all()
 *
 * @brief  Waits for all jobs to complete, so that we may reap all children
 *         properly. This is only intended to be used after cancel_all_jobs()
 *         is called, so that we may properly reap all child processes before
 *         exiting the shell.
 *
 * @return  0 on success, -errno on error
 **/
int wait_for_all()
{
    DEBUG("wait_for_all() - ENTER");
    int retval = 0;

    job_t *j = jobs;

    pid_t pid;
    int status;

    while(j)
    {
        /* live process? */
        if(j->status == RUNNING || j->status == SUSPENDED || j->status == CANCELED)
        {
            while((pid = waitpid(j->pgid, &status, 0)) < 0)
            {
                if(errno == EINTR)
                    continue;

                /* since we're probably being called from the exit callback, we
                 * can't exit() here. use _exit(). */
                _exit(-1);

            }

            if(WIFEXITED(status))
            {
                j->status = EXITED;
                j->exitcode = WEXITSTATUS(status);
            }
            else if(WIFSIGNALED(status))
            {
                j->status = ABORTED;
                j->exitcode = WTERMSIG(status);
            }
        }

        j = j->next;
    }

    DEBUG("wait_for_all() - END [%d]", retval);
    return retval;
}

/**
 * int print_job(job_t *)
 *
 * @brief  Prints information about a job
 *
 * @param j  The job to output
 *
 * @return  0 on success, -errno on error.
 **/
int print_job(job_t *j)
{
    DEBUG("print_job() - ENTER [job @ %p]", j);
    int retval = 0;

    VALIDATE(j,
            "job must be non-NULL",
            -EINVAL,
            print_job_end);

    if(j->status != EXITED && j->status != ABORTED)
    {
        if(dprintf(STDOUT_FILENO, "[%d] (%s) %s\n",
                    j->jobid, jobs_status_as_char(j->status), j->ui->input) < 0)
        {
            retval = -1;
            goto print_job_end;
        }
    }
    else
    {
        if(dprintf(STDOUT_FILENO, "[%d] (%s <%d>) %s\n",
                    j->jobid, jobs_status_as_char(j->status), j->exitcode,
                    j->ui->input) < 0)
        {
            retval = -1;
            goto print_job_end;
        }
    }

print_job_end:
    DEBUG("print_job() - EXIT [%d]", retval);
    return retval;
}

/**
 * int jobs_list()
 *
 * @brief  Outputs a neatly formatted list of all jobs our shell is currently
 *         responsible for.
 *
 * @return  0 on success, -1 on error
 **/
int jobs_list()
{
    DEBUG("jobs_list() - ENTER");
    int retval = 0;

    job_t *j = jobs;
    while(j)
    {
        VALIDATE(print_job(j) == 0,
                "print_job() failed to output job information",
                -1,
                jobs_list_end);
        if(j->status == EXITED || j->status == ABORTED)
        {
            job_t *jn = j->next;
            jobs_remove(j);
            j = jn;
        }
        else
            j = j->next;
    }

jobs_list_end:
    DEBUG("jobs_list() - EXIT [%d]", retval);
    return retval;
}

/**
 * char* jobs_status_as_char(int)
 *
 * @brief  Converts a job status as an integer to a string representation.
 *
 * @param status  The status to convert
 *
 * @return  The string corresponding to the status, or NULL if not valid.
 **/
char* jobs_status_as_char(int status)
{
    switch(status)
    {
        case NEW:
            return "new";

        case RUNNING:
            return "running";

        case SUSPENDED:
            return "suspended";

        case EXITED:
            return "exited";

        case ABORTED:
            return "aborted";

        case CANCELED:
            return "canceled";

        default:
            return NULL;
    }
}
