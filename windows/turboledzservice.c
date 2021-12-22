//
// turboledzd.c
//
// Daemon for the Turbo LEDz devices.
// (c)2021 Game Studio Abraham Stolk Inc.
//

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <wchar.h>		// hidapi uses wide characters.
#include <inttypes.h>
#if !defined(_WIN32)
	#include <unistd.h>
#endif
#include <string.h>
#include <float.h>

#include <errno.h>
#if defined(_WIN32)
#	define EX_NOINPUT EXIT_FAILURE
#	define EX_NOPERM EXIT_FAILURE
#	define EX_IOERR	EXIT_FAILURE
#else
#	include <sysexits.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <windows.h>

#include "cpuinf.h"
#include "turboledz.h"


static void sig_handler( int signum )
{
#if 0
	if ( signum == SIGHUP )
	{
		// Re-read the configution file!
		read_config();
	}
#endif
	if ( signum == SIGTERM || signum == SIGINT )
	{
		// Shut down the daemon and exit cleanly.
		fprintf(stderr, "Attempting to close down gracefully...\n");
		turboledz_cleanup();
		exit(0);
	}
#if 0
	if ( signum == SIGUSR1 )
	{
		// This signals that the host is about to go to sleep / suspend.
		turboledz_pause_all_devices();
	}
	if ( signum == SIGUSR2 )
	{
		fprintf( stderr, "Woken up.\n" );
		turboledz_paused = 0;
	}
#endif
}



int main( int argc, char* argv[] )
{
	(void)argc;
	(void)argv;
	fprintf(stderr,"Turbo LEDZ daemon. (c) by GSAS Inc.\n");
	strncpy( opt_mode, "cpu", sizeof(opt_mode) );
	//turboledz_read_config();

	if ( opt_launchpause > 0 )
	{
		fprintf(stderr, "A %dms graceperiod for udevd to do its work starts now.\n", opt_launchpause);
		Sleep(opt_launchpause);
		fprintf(stderr, "Commencing...\n");
	}

	signal( SIGINT,  sig_handler ); // For graceful exit.
	signal( SIGTERM, sig_handler );	// For graceful exit.
	//signal( SIGHUP,  sig_handler );	// For re-reading configuration.
	//signal( SIGUSR1, sig_handler );	// For going to sleep.
	//signal( SIGUSR2, sig_handler );	// For waking up.

	int initres = turboledz_init();
	if (initres)
		return initres;

	int rv = turboledz_service();
	turboledz_cleanup();
	return rv;
}

