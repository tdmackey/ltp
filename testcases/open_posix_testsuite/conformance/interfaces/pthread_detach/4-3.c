/*
 * Copyright (c) 2004, Bull S.A..  All rights reserved.
 * Created by: Sebastien Decugis
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * This sample test aims to check the following assertion:
 *
 * The function does not return EINTR
 *
 * The steps are:
 * -> kill a thread which calls pthread_detach()
 * -> check that EINTR is never returned
 *
 */

#define _POSIX_C_SOURCE 200112L

/* Some routines are part of the XSI Extensions */
#ifndef WITHOUT_XOPEN
#define _XOPEN_SOURCE	600
#endif

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <semaphore.h>
#include <errno.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>

#include "../testfrmw/testfrmw.h"
#include "../testfrmw/testfrmw.c"

#define SAFE(op) \
	do {\
		if ((op) == -1) \
			UNRESOLVED(errno, "unexpected failure"); \
	} while (0)
#define RUN_TIME_USEC (2*1000*1000)
#define SIGNALS_WITHOUT_DELAY 100
#ifndef VERBOSE
#define VERBOSE 1
#endif

#define WITH_SYNCHRO

#include "../testfrmw/threads_scenarii.c"

static volatile int do_it1 = 1;
static unsigned long count_ope;
#ifdef WITH_SYNCHRO
static sem_t semsig1;
#endif

static unsigned long count_sig;
static unsigned long sleep_time;
static sigset_t usersigs;

struct thestruct {
	int	sig;
#ifdef WITH_SYNCHRO
	sem_t	*sem;
#endif
};

unsigned long current_time_usec()
{
	struct timeval now;
	SAFE(gettimeofday(&now, NULL));
	return now.tv_sec * 1000000 + now.tv_usec;
}

/* the following function keeps on sending the signal to the process */
static void *sendsig(void *arg)
{
	struct thestruct *thearg = (struct thestruct *)arg;
	int ret;
	pid_t process;

	process = getpid();

	/* We block the signal SIGUSR1 for this THREAD */
	ret = pthread_sigmask(SIG_BLOCK, &usersigs, NULL);
	if (ret != 0)
		UNRESOLVED(ret, "Unable to block SIGUSR1 in signal thread");

	while (do_it1) {
#ifdef WITH_SYNCHRO
		ret = sem_wait(thearg->sem);
		if (ret)
			UNRESOLVED(errno, "Sem_wait in sendsig");
		if (!do_it1)
			break;

		count_sig++;
#endif
		/* keep increasing sleeptime to make sure we progress
		 * allow SIGNALS_WITHOUT_DELAY signals without any pause,
		 * then start increasing sleep_time to make sure all threads
		 * can progress */
		sleep_time++;
		if (sleep_time / SIGNALS_WITHOUT_DELAY > 0)
			usleep(sleep_time / SIGNALS_WITHOUT_DELAY);

		ret = kill(process, thearg->sig);
		if (ret != 0)
			UNRESOLVED(errno, "Kill in sendsig");
	}
	return NULL;
}

static void sighdl1(int sig)
{
#ifdef WITH_SYNCHRO
	if (sem_post(&semsig1))
		UNRESOLVED(errno, "Sem_post in signal handler 1");
#endif
}

static void *threaded(void *arg)
{
	int ret;

	/* We don't block the signal SIGUSR1 for this THREAD */
	ret = pthread_sigmask(SIG_UNBLOCK, &usersigs, NULL);
	if (ret != 0)
		UNRESOLVED(ret, "Unable to unblock SIGUSR1 in worker thread");

	ret = pthread_detach(pthread_self());
	if (ret == EINTR)
		FAILED("pthread_detach() returned EINTR");

	/* Signal we're done */
	do {
		ret = sem_post(&scenarii[sc].sem);
	} while ((ret == -1) && (errno == EINTR));
	if (ret == -1)
		UNRESOLVED(errno, "Failed to wait for the semaphore");

	return arg;
}

static void *test(void *arg)
{
	int ret = 0;
	pthread_t child;

	/* We block the signal SIGUSR1 for this THREAD */
	ret = pthread_sigmask(SIG_BLOCK, &usersigs, NULL);
	if (ret != 0)
		UNRESOLVED(ret, "Unable to block SIGUSR1 in signal thread");

	for (sc = 0; sc < NSCENAR; sc++) {
#if VERBOSE > 5
		output("-----\n");
		output("Starting test with scenario (%i): %s\n",
		       sc, scenarii[sc].descr);
#endif
		/* reset sleep time for signal thread */
		sleep_time = 0;

		/* thread which is already detached is not interesting
		 * for this test, skip it */
		if (scenarii[sc].detached == 1)
			continue;

		ret = pthread_create(&child, &scenarii[sc].ta, threaded, NULL);
		switch (scenarii[sc].result) {
		case 0: /* Operation was expected to succeed */
			if (ret != 0)
				UNRESOLVED(ret, "Failed to create this thread");
			break;

		case 1: /* Operation was expected to fail */
			if (ret == 0)
				UNRESOLVED(-1, "An error was expected but the"
					   " thread creation succeeded");
			break;

		case 2: /* We did not know the expected result */
		default:
#if VERBOSE > 5
			if (ret == 0)
				output("Thread has been created successfully"
				       " for this scenario\n");
			else
				output("Thread creation failed with the error:"
				       " %s\n", strerror(ret));
#endif
			;
		}
		if (ret == 0) {
			/* Just wait for the thread to terminate */
			do {
				ret = sem_wait(&scenarii[sc].sem);
			} while ((ret == -1) && (errno == EINTR));
			if (ret == -1)
				UNRESOLVED(errno, "Failed to wait for the"
					   " semaphore");
		}
	}
	return NULL;
}

void do_child()
{
	int ret;
	pthread_t th_work, th_sig1;
	struct thestruct arg1;
	struct sigaction sa;

	/* We prepare a signal set which includes SIGUSR1 */
	sigemptyset(&usersigs);
	ret = sigaddset(&usersigs, SIGUSR1);
	if (ret != 0)
		UNRESOLVED(ret, "Unable to add SIGUSR1 or 2 to a signal set");

	/* We now block the signal SIGUSR1 for this THREAD */
	ret = pthread_sigmask(SIG_BLOCK, &usersigs, NULL);
	if (ret != 0)
		UNRESOLVED(ret, "Unable to block SIGUSR1 in main thread");

	/* We need to register the signal handlers for the PROCESS */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = sighdl1;

	ret = sigaction(SIGUSR1, &sa, NULL);
	if (ret)
		UNRESOLVED(ret, "Unable to register signal handler1");

	arg1.sig = SIGUSR1;
#ifdef WITH_SYNCHRO
	if (sem_init(&semsig1, 0, 1))
		UNRESOLVED(errno, "Semsig1 init");
	arg1.sem = &semsig1;
#endif

	count_sig = 0;
	ret = pthread_create(&th_sig1, NULL, sendsig, &arg1);
	if (ret)
		UNRESOLVED(ret, "Signal 1 sender thread creation failed");

	ret = pthread_create(&th_work, NULL, test, NULL);
	if (ret)
		UNRESOLVED(ret, "Worker thread creation failed");

	ret = pthread_join(th_work, NULL);
	if (ret)
		UNRESOLVED(ret, "Worker thread join failed");

	/* to be extra safe unblock signals in this thread,
	 * so signal thread won't get stuck on sem_wait */
	ret = pthread_sigmask(SIG_UNBLOCK, &usersigs, NULL);
	if (ret != 0)
		UNRESOLVED(ret, "Failed to unblock signals");
	do_it1 = 0;
#ifdef WITH_SYNCHRO
	/* if it's already stuck on sem_wait, up the semaphore */
	if (sem_post(&semsig1))
		UNRESOLVED(errno, "Sem_post in do_child");
#endif
	ret = pthread_join(th_sig1, NULL);
	if (ret)
		UNRESOLVED(ret, "Worker thread join failed");
}

void main_loop()
{
	int child_count = 0;
	int stat_pipe[2];
	int ret;
	int status;
	pid_t child;
	unsigned long usec_start, usec;
	unsigned long child_count_sig;

	usec_start = current_time_usec();
	do {
		SAFE(pipe(stat_pipe));

		child_count++;
		count_ope += NSCENAR;
		fflush(stdout);
		child = fork();
		if (child == 0) {
			close(stat_pipe[0]);
			do_child();
			SAFE(write(stat_pipe[1], &count_sig,
				sizeof(count_sig)));
			close(stat_pipe[1]);
			pthread_exit(0);
			UNRESOLVED(0, "Should not be reached");
			exit(0);
		}
		close(stat_pipe[1]);
		SAFE(read(stat_pipe[0], &child_count_sig, sizeof(count_sig)));
		close(stat_pipe[0]);
		count_sig += child_count_sig;

		ret = waitpid(child, &status, 0);
		if (ret != child)
			UNRESOLVED(errno, "Waitpid returned the wrong PID");

		if (!WIFEXITED(status)) {
			output("status: %d\n", status);
			FAILED("Child exited abnormally");
		}

		if (WEXITSTATUS(status) != 0) {
			output("exit status: %d\n", WEXITSTATUS(status));
			FAILED("An error occured in child");
		}

		usec = current_time_usec();
	} while ((usec - usec_start) < RUN_TIME_USEC);
	output("Test spawned %d child processes.\n", child_count);
	output("Test finished after %lu usec.\n", usec - usec_start);
}

int main(int argc, char *argv[])
{
	output_init();
	scenar_init();
	main_loop();
	scenar_fini();

#if VERBOSE > 0
	output("Test executed successfully.\n");
	output("  %d thread detached.\n", count_ope);
#ifdef WITH_SYNCHRO
	output("  %d signals were sent meanwhile.\n", count_sig);
#endif
#endif
	PASSED;
}
