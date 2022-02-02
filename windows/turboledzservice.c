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
#include <powrprof.h>	// For DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS

#include "cpuinf.h"
#include "turboledz.h"

static SERVICE_STATUS			servstat = { 0 };

static SERVICE_STATUS_HANDLE	sshandle = NULL;

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
		turboledz_pause_all_devices();
		servstat.dwCurrentState = SERVICE_PAUSED;
		if (SetServiceStatus(sshandle, &servstat) == FALSE)
			LOGI("SetServiceStatus() failed for SERVICE_PAUSED.");
		break;
	case SERVICE_CONTROL_CONTINUE:
		LOGI("SERVICE_CONTROL_CONTINUE");
		turboledz_paused = 0;
		servstat.dwCurrentState = SERVICE_RUNNING;
		if (SetServiceStatus(sshandle, &servstat) == FALSE)
			LOGI("SetServiceStatus() failed for SERVICE_RUNNING.");
		break;
	case SERVICE_CONTROL_SHUTDOWN:
		LOGI("SERVICE_CONTROL_SHUTDOWN");
		turboledz_pause_all_devices();
		break;
	case SERVICE_CONTROL_POWEREVENT:
		LOGI("SERVICE_CONTROL_POWEREVENT");
		LOGI("dwEventType 0x%lx", dwEventType);
		if (dwEventType == PBT_POWERSETTINGCHANGE)
		{
			//POWERBROADCAST_SETTING* setting = (POWERBROADCAST_SETTING*)lpEventData;
		}
		break;
	case SERVICE_CONTROL_STOP:
		LOGI("SERVICE_CONTROL_STOP");

		servstat.dwControlsAccepted = 0;
		servstat.dwCurrentState = SERVICE_STOP_PENDING;
		servstat.dwWin32ExitCode = 0;
		servstat.dwCheckPoint = 4;

		if (SetServiceStatus(sshandle, &servstat) == FALSE)
			LOGI("SetServiceStatus() failed for SERVICE_STOP_PENDING.");

		// This will make the turboledz_service() function return.
		turboledz_finished = 1;

		break;

	default:
		break;
	}
	return 0;
}


#if 1
static ULONG DeviceNotifyCallbackRoutine
(
	PVOID Context,
	ULONG Type,			// PBT_APMSUSPEND, PBT_APMRESUMESUSPEND, or PBT_APMRESUMEAUTOMATIC
	PVOID Setting		// Unused
)
{
	LOGI("DeviceNotifyCallbackRoutine");
	if (Type == PBT_APMSUSPEND)
	{
		turboledz_pause_all_devices();
		LOGI("Devices paused.");
	}
	if (Type == PBT_APMRESUMEAUTOMATIC)
	{
		turboledz_paused = 0;
		LOGI("Device unpaused.");
	}
	return 0;
}

static DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS notifycb =
{
	DeviceNotifyCallbackRoutine,
	NULL,
};
#endif


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

	// Subscribe to suspend/resume notifications.
	HPOWERNOTIFY registration;
	const DWORD registered = PowerRegisterSuspendResumeNotification
	(
		DEVICE_NOTIFY_CALLBACK,
		&notifycb,
		&registration
	);
	if (registered != ERROR_SUCCESS)
	{
		const DWORD err = GetLastError();
		LOGI("PowerRegisterSuspendResumeNotification failed with error 0x%lx", err);
	}

	// Tell the service controller we are starting
	memset(&servstat, 0, sizeof(servstat));
	servstat.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	servstat.dwControlsAccepted = 0;
	servstat.dwCurrentState = SERVICE_START_PENDING;
	servstat.dwWin32ExitCode = 0;
	servstat.dwServiceSpecificExitCode = 0;
	servstat.dwCheckPoint = 0;

	if (SetServiceStatus(sshandle, &servstat) == FALSE)
		LOGI("SetServiceStatus() failed when setting SERVICE_START_PENDING.");

	/*
	 * Perform tasks neccesary to start the service here
	 */
	LOGI("Calling turboledz_init()");
	int initres = turboledz_init(logf);
	if (initres)
	{
		LOGI("turboledz_init() failed, and returned %d", initres);
		// Tell SCM that we were not able to start.
		servstat.dwCurrentState = SERVICE_STOPPED;
		servstat.dwWin32ExitCode = 99;
		if (SetServiceStatus(sshandle, &servstat) == FALSE)
			LOGI("SetServiceStatus() failed when setting SERVICE_STOPPED.");
		return;
	}
	else
	{
		LOGI("turboledz_init() succeeded and found %d/%d virtual/physical cpus.",
			cpuinf_num_virtual_cores,
			cpuinf_num_physical_cores
		);
	}

	// Tell the service controller we are started
	servstat.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_SHUTDOWN;
	servstat.dwCurrentState = SERVICE_RUNNING;
	servstat.dwWin32ExitCode = 0;
	servstat.dwCheckPoint = 0;

	if (SetServiceStatus(sshandle, &servstat) == FALSE)
	{
		LOGI("SetServiceStatus() failed when setting SERVICE_RUNNING.");
		return;
	}

	const int serviceres = turboledz_service(); // This will return once the controller sets turboledz_finished to non-zero.
	LOGI("turboledz_service() returned %d", serviceres);

	// The servicing is done. We should clean up.
	turboledz_cleanup();

	// Tell SCM about our STOPPED status.
	servstat.dwControlsAccepted = 0;
	servstat.dwCurrentState = SERVICE_STOPPED;
	servstat.dwWin32ExitCode = 0;
	servstat.dwCheckPoint = 3;

	if (SetServiceStatus(sshandle, &servstat) == FALSE)
		LOGI("SetServiceStatus() failed when setting SERVICE_STOPPED.");

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

	return 0;
}

