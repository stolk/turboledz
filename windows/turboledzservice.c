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

static SERVICE_STATUS			servstat = { 0 };

static SERVICE_STATUS_HANDLE	sshandle = NULL;

static HANDLE					stopev = INVALID_HANDLE_VALUE;


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


DWORD WINAPI ServiceCtrlHandler
(
	DWORD CtrlCode,
	DWORD dwEventType,
	LPVOID lpEventData,
	LPVOID lpContext
)
{
	fprintf(stderr, "Control Handler\n");
	return 0;
}


VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
	sshandle = RegisterServiceCtrlHandlerExA
	(
		"turboledz",
		ServiceCtrlHandler,
		(LPVOID)0
	);
	if (sshandle == NULL)
	{
		const DWORD err = GetLastError();
		fprintf(stderr, "RegisterServiceCtrlHandlerExA() failed with error 0x%lx\n", err);
		return;
	}
	else
	{
		fprintf(stderr, "Service Control Handler for TurboLEDz has been registered.\n");
	}

	// Tell the service controller we are starting
	ZeroMemory(&servstat, sizeof(servstat));
	servstat.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	servstat.dwControlsAccepted = 0;
	servstat.dwCurrentState = SERVICE_START_PENDING;
	servstat.dwWin32ExitCode = 0;
	servstat.dwServiceSpecificExitCode = 0;
	servstat.dwCheckPoint = 0;

	if (SetServiceStatus(sshandle, &servstat) == FALSE)
	{
		fprintf(stderr, "SetServiceStatus() failed\n");
		return;
	}

	/*
	 * Perform tasks neccesary to start the service here
	 */


	// Create stop event to wait on later.
	stopev = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (stopev == NULL)
	{
		fprintf(stderr, "CreateEvent() returned error %lx\n", GetLastError());

		servstat.dwControlsAccepted = 0;
		servstat.dwCurrentState = SERVICE_STOPPED;
		servstat.dwWin32ExitCode = GetLastError();
		servstat.dwCheckPoint = 1;

		if (SetServiceStatus(sshandle, &servstat) == FALSE)
		{
			fprintf(stderr, "SetServiceStatus() failed\n");
		}
		return;
	}

	// Tell the service controller we are started
	servstat.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	servstat.dwCurrentState = SERVICE_RUNNING;
	servstat.dwWin32ExitCode = 0;
	servstat.dwCheckPoint = 0;

	if (SetServiceStatus(sshandle, &servstat) == FALSE)
	{
		fprintf(stderr, "SetServiceStatus() failed\n");
		return;
	}

#if 0
	// Start the thread that will perform the main task of the service
	HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
	OutputDebugString(_T("My Sample Service: ServiceMain: Waiting for Worker Thread to complete"));
	// Wait until our worker thread exits effectively signaling that the service needs to stop
	WaitForSingleObject(hThread, INFINITE);
	OutputDebugString(_T("My Sample Service: ServiceMain: Worker Thread Stop Event signaled"));
#endif

	Sleep(5000);

	/*
	 * Perform any cleanup tasks
	 */

	CloseHandle(stopev);

	servstat.dwControlsAccepted = 0;
	servstat.dwCurrentState = SERVICE_STOPPED;
	servstat.dwWin32ExitCode = 0;
	servstat.dwCheckPoint = 3;

	if (SetServiceStatus(sshandle, &servstat) == FALSE)
	{
		fprintf(stderr, "SetServiceStatus() failed\n");
	}

	return;
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

	SERVICE_TABLE_ENTRY servtabl[] =
	{
		{L"turboledz", (LPSERVICE_MAIN_FUNCTION)ServiceMain},
		{NULL, NULL}
	};
	const int startres = StartServiceCtrlDispatcher( servtabl );
	if (!startres)
	{
		const DWORD err = GetLastError();
		fprintf(stderr, "StartServiceCtrlDispatcher() failed with: 0x%lx\n", err);
		exit(1);
	}

	int initres = turboledz_init();
	if (initres)
		return initres;

	int rv = turboledz_service();
	turboledz_cleanup();
	return rv;
}

