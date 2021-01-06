/**
 * @file jobs.h
 * @author Daniel Calabria
 *
 * Header file for jobs.c
 **/

#ifndef JOBS_H
#define JOBS_H

#include <termios.h>
#ifdef EXTRA_CREDIT
#include <sys/time.h>
#endif

#include "parse.h"

/* The various states that our jobs can be in */
#define NEW         0
#define RUNNING     1
#define SUSPENDED   2
#define EXITED      3
#define ABORTED     4
#define CANCELED    5


/**
 * Our job structure. This tracks information about each job.
 **/
typedef struct job_s
{
    user_input_t *ui;

    int status;
    int exitcode;
    int pgid;
    int jobid;
    struct termios termattr;
    int is_background_job;
    int is_in_bg;

#ifdef EXTRA_CREDIT
    struct timeval starttime;
#endif

    struct job_s *next;
} job_t;

/* exported list structure of all jobs */
extern job_t *jobs;

/**
 * The exit value of the last completed foreground command (if running
 * interactively, or the last completed command (if running non-interactively)
 **/
extern int last_exit_code;

/* fxn prototypes for jobs.c */
job_t* jobs_create(user_input_t *ui);
int jobs_insert(job_t *);
int jobs_remove(job_t *);
int jobs_list();
job_t* jobs_lookup_by_jobid(int jobid);
char* jobs_status_as_char(int);
void free_jobs();
int exec_job(job_t *job);
int cancel_all_jobs();
int wait_for_all();
int job_update_status(job_t *job, int status);
int print_job(job_t *j);
int run_in_background(job_t *job, int cont);
int run_in_foreground(job_t *job, int cont);

#endif // JOBS_H
