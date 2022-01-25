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
#include <unistd.h>
#include <string.h>
#include <float.h>
#include <errno.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <hidapi/hidapi.h>

#include "cpuinf.h"
#include "turboledz.h"


// A systemd daemon needs to be able to re-read its config on SIGHUP, so we do that here.
static int read_config(void)
{
	const char* fname = "/etc/turboledz.conf";
	FILE* f = fopen( fname, "r" );
	if ( !f )
	{
		fprintf( stderr, "Config file '%s' not found.\n", fname );
		return 0;
	}
	char line[1024];
	int parsed=0;
	while ( 1 )
	{
		char* s = fgets( line, sizeof(line)-1, f );
		if ( !s )
		{
			fprintf( stderr, "Parsed %d options from config file.\n", parsed );
			return parsed;
		}
		if ( strstr( s, "=" ) )
		{
			if ( s[0] != '#' )
			{
				const size_t l = strlen(s);
				if ( l>0 && s[l-1]=='\n' )
					s[l-1] = 0;
				if ( !strncmp( s, "freq=", 5 ) )
				{
					int freq = atoi( s+5 );
					if ( freq > 0 && freq <= 100 )
						opt_freq = freq;
					parsed++;
				}
				if ( !strncmp( s, "mode=", 5 ) )
				{
					strncpy( opt_mode, s+5, sizeof(opt_mode)-1 );
					parsed++;
				}
				if ( !strncmp( s, "model=", 6 ) )
				{
					strncpy( opt_model, s+6, sizeof(opt_model)-1 );
					parsed++;
				}
				if ( !strncmp( s, "launchpause=", 12 ) )
				{
					opt_launchpause = atoi( s+12 );
					parsed++;
				}
			}
		}
	}
	return parsed;
}


static void sig_handler( int signum )
{
	if ( signum == SIGHUP )
	{
		// Re-read the configution file!
		read_config();
	}
	if ( signum == SIGTERM || signum == SIGINT )
	{
		// Shut down the daemon and exit cleanly.
		fprintf(stderr, "Attempting to close down gracefully...\n");
		turboledz_cleanup();
		exit(0);
	}
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
}


int main( int argc, char* argv[] )
{
	(void)argc;
	(void)argv;
	fprintf(stderr,"Turbo LEDZ daemon. (c) by GSAS Inc.\n");
	strncpy( opt_mode, "cpu", sizeof(opt_mode) );
	read_config();

	if ( opt_launchpause > 0 )
	{
		fprintf(stderr, "A %dms graceperiod for udevd to do its work starts now.\n", opt_launchpause);
		usleep( opt_launchpause * 1000 );
		fprintf(stderr, "Commencing...\n");
	}

	int initresult = turboledz_init();
	if ( initresult )
		return initresult;

	signal( SIGINT,  sig_handler ); // For graceful exit.
	signal( SIGTERM, sig_handler );	// For graceful exit.
	signal( SIGHUP,  sig_handler );	// For re-reading configuration.
	signal( SIGUSR1, sig_handler );	// For going to sleep.
	signal( SIGUSR2, sig_handler );	// For waking up.

	int rv = turboledz_service();
	turboledz_cleanup();
	return rv;
}

