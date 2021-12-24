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

static FILE*					logf = 0;

#define LOGI(...) \
{ \
printf(__VA_ARGS__); \
printf("\n"); \
fflush(stdout); \
if (logf) { fprintf(logf, __VA_ARGS__); fprintf(logf, "\n"); fflush(logf); } \
}


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
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_PAUSE:
		LOGI("SERVICE_CONTROL_PAUSE");
		break;
	case SERVICE_CONTROL_CONTINUE:
		LOGI("SERVICE_CONTROL_CONTINUE");
		break;
	case SERVICE_CONTROL_SHUTDOWN:
		LOGI("SERVICE_CONTROL_SHUTDOWN");
		break;
	case SERVICE_CONTROL_POWEREVENT:
		LOGI("SERVICE_CONTROL_POWEREVENT");
		if (dwEventType == PBT_POWERSETTINGCHANGE)
		{
			//POWERBROADCAST_SETTING* setting = (POWERBROADCAST_SETTING*)lpEventData;
		}
		break;
	case SERVICE_CONTROL_STOP:
		LOGI("SERVICE_CONTROL_STOP");

		if (servstat.dwCurrentState != SERVICE_RUNNING)
			break;
		/*
		 * Perform tasks necessary to stop the service here
		 */

		servstat.dwControlsAccepted = 0;
		servstat.dwCurrentState = SERVICE_STOP_PENDING;
		servstat.dwWin32ExitCode = 0;
		servstat.dwCheckPoint = 4;

		if (SetServiceStatus(sshandle, &servstat) == FALSE)
			LOGI("SetServiceStatus() failed for SERVICE_STOP_PENDING.");

		// This will signal the worker thread to start shutting down
		SetEvent(stopev);

		break;

	default:
		break;
	}
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
		LOGI("RegisterServiceCtrlHandlerExA() failed with error 0x%lx", err);
		return;
	}
	else
	{
		LOGI("Service Control Handler for TurboLEDz has been registered.");
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
		LOGI("SetServiceStatus() failed when setting SERVICE_START_PENDING.");
		return;
	}

	/*
	 * Perform tasks neccesary to start the service here
	 */

	// Create stop event to wait on later.
	stopev = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (stopev == NULL)
	{
		LOGI("CreateEvent() returned error %lx", GetLastError());

		servstat.dwControlsAccepted = 0;
		servstat.dwCurrentState = SERVICE_STOPPED;
		servstat.dwWin32ExitCode = GetLastError();
		servstat.dwCheckPoint = 1;

		if (SetServiceStatus(sshandle, &servstat) == FALSE)
		{
			LOGI("SetServiceStatus() failed when setting SERVICE_STOPPED.");
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
		LOGI("SetServiceStatus() failed when setting SERVICE_RUNNING.");
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

	Sleep(50000);

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
		LOGI("SetServiceStatus() failed when setting SERVICE_STOPPED.");
	}

	return;
}

static int is_elevated()
{
	int elevated = 0;
	HANDLE hToken = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		TOKEN_ELEVATION Elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize))
			elevated = Elevation.TokenIsElevated;
	}
	else
	{
		LOGI("OpenProcessToken() failed.");
	}
	if (hToken)
		CloseHandle(hToken);
	return elevated;
}


int main( int argc, char* argv[] )
{
	(void)argc;
	(void)argv;

	char dirname[1024];
	memset(dirname, 0, sizeof(dirname));
#if 0
	const char* s = strstr(argv[0], "turboledzservice.exe");
	assert(s);
	const size_t len = s - argv[0];
	assert(len > 0 && len < 1024);
	strncpy(dirname, argv[0], len);
#else
	strncpy(dirname, "c:\\temp\\", sizeof(dirname));
#endif
	fprintf(stderr, "dir = %s\n", dirname);
	char logname[1024];
	snprintf(logname, sizeof(logname), "%s%s", dirname, "turboledzservice.log");
	logf = fopen(logname, "wb");

	LOGI("Turbo LEDZ daemon. (c) by GSAS Inc.");
	strncpy( opt_mode, "cpu", sizeof(opt_mode) );
	//turboledz_read_config();

	const int elevated = is_elevated();
	if (!elevated)
	{
		LOGI("Not running with elevated priviledge.");
	}

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
		if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
			LOGI("The program is being run as a console application rather than as a service.");
		LOGI("StartServiceCtrlDispatcher() failed with: 0x%lx", err);
		return 1;
	}

	int initres = turboledz_init();
	if (initres)
		return initres;

	int rv = turboledz_service();
	turboledz_cleanup();
	return rv;
}

