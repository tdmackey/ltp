/******************************************************************************
 *
 *   Copyright  International Business Machines  Corp., 2007
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * NAME
 *      test-skeleton.c
 *
 * DESCRIPTION
 *
 *
 *
 * USAGE:
 *      Use run_auto.sh script in current directory to build and run test.
 *      Use "-j" to enable jvm simulator.
 *
 *      Compilation : gcc -lrt -lpthread testpi-0.c
 *
 * AUTHOR
 *
 *
 * HISTORY
 *
 *
 *****************************************************************************/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <librttest.h>
#include <libjvmsim.h>

static int run_jvmsim=0;

void usage(void)
{
        rt_help();
        printf("testpi-5 and 6 specific options:\n");
        printf("  -j            enable jvmsim\n");
}

int parse_args(int c, char *v)
{

        int handled = 1;
        switch (c) {
                case 'j':
                        run_jvmsim = 1;
                        break;
                case 'h':
                        usage();
                        exit(0);
                default:
                        handled = 0;
                        break;
        }
        return handled;
}

#define TEST_FUNCTION do_test(argc, argv)
#define TIMEOUT 20

static pid_t pid;

static void timeout_handler (int sig)
{
	int i, killed, status;

	printf("Inside the timeout handler, killing the TC threads \n");
	kill( pid, SIGKILL );
	for(i=0; i<5; i++) {
		killed = waitpid(pid, &status, WNOHANG|WUNTRACED);
		if (0 != killed)
			break;

		struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
      		nanosleep (&ts, NULL);
	}

	if (0 != killed && pid != killed ) {
		printf("\n Failed to kill child process ");
		exit(1);
	}
	printf("\nResult:PASS\n");
	exit(1);
}

int  main(int argc, char **argv)
{
	pid_t termpid;
 	int status;
	setup();

	rt_init("jh",parse_args,argc,argv);

	if (run_jvmsim) {
	        printf("jvmsim enabled\n");
	        jvmsim_init();  // Start the JVM simulation
	} else {
              printf("jvmsim disabled\n");
	}

	pid = fork();
	if ( 0 == pid) {
						//This is the child
		exit (TEST_FUNCTION);
	}
	else if (pid < 0) {
		printf("\n Cannot fork test program \n");
		exit(1);
	}

	signal( SIGALRM, timeout_handler);
	alarm(TIMEOUT);
	termpid = TEMP_FAILURE_RETRY (waitpid (pid, &status, 0));
	if ( -1 == termpid ) {
		printf("\n Waiting for test program failed, Exiting \n");
		exit(1);
	}

	return 0;
}
