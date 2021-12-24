#include <windows.h>
#include <stdio.h>
#include <assert.h>

static FILE* logf;

#define LOGI(...) \
{ \
printf(__VA_ARGS__); \
printf("\n"); \
fflush(stdout); \
if (logf) { fprintf(logf, __VA_ARGS__); fprintf(logf, "\n"); fflush(logf); } \
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

const char* statenames[] =
{
	"UNKNOWN",
	"SERVICE_STOPPED",
	"SERVICE_START_PENDING",
	"SERVICE_STOP_PENDING",
	"SERVICE_RUNNING",
	"SERVICE_CONTINUE_PENDING",
	"SERVICE_PAUSE_PENDING",
	"SERVICE_PAUSED",
	0,
};


int main( int argc, char* argv[] )
{
	logf = fopen("c:/temp/turboledzinstall.log", "wb");

	const int doremove = (argc > 1 && !strcmp(argv[1], "/Uninstall")) ? 1 : 0;
	const int elevated = is_elevated();

	if (!elevated)
	{
		LOGI("Not running with elevated permissions. Aborting.");
		exit(1);
	}

	char dirname[1024];
	memset(dirname, 0, sizeof(dirname));
	const char* s = strstr(argv[0], "turboledzinstall.exe");
	LOGI("argv[0] = %s", argv[0]);
	assert(s);
	const size_t len = s - argv[0];
	assert(len > 0 && len < 1024);
	strncpy(dirname, argv[0], len);
	//fprintf(stderr, "dir = %s\n", dirname);

	char servicepath[1024];
	memset(servicepath, 0, sizeof(servicepath));

	snprintf(servicepath, sizeof(servicepath), "%s%s", dirname, "turboledzservice.exe");
	FILE* f = fopen(servicepath, "rb");
	if (!f)
	{
		LOGI("Did not find turboledzservice executable at %s. Aborting.", servicepath);
		return 2;
	}
	assert(f);
	LOGI("Found executable at %s", servicepath);
	fclose(f);

	SC_HANDLE scm = OpenSCManagerA
	(
		NULL, // Machine name
		NULL, // Database name
		SC_MANAGER_ALL_ACCESS
	);

	if (!scm)
	{
		LOGI("Failed to open Service Configuration Manager. Aborting.");
		return 3;
	}

	if (doremove)
	{
		SC_HANDLE service = OpenServiceA
		(
			scm,
			"turboledz",
			SC_MANAGER_ALL_ACCESS
		);
		if (!service)
		{
			LOGI("Failed to open service.");
			return 4;
		}
		DeleteService(service);
		CloseServiceHandle(service);
		CloseServiceHandle(scm);
		return 0;
	}
	
	// If we are not removing the service, we are creating it.
	SC_HANDLE service = CreateServiceA
	(
		scm,
		"turboledz",
		"TurboLEDz",
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		servicepath,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	);
	if (service)
	{
		LOGI("turboledz service has been created.");
	}
	else
	{
		DWORD err = GetLastError();
		LOGI("Failed to create service. Error: 0x%lx", err);
		return 5;
	}

	// Query the service.
	SERVICE_STATUS servstat;
	const int queried = QueryServiceStatus(service, &servstat);
	if (!queried)
	{
		const DWORD err = GetLastError();
		LOGI("Failed to query service status. Err = 0x%lx", err);
	}
	else
	{
		if ( servstat.dwCurrentState <= SERVICE_PAUSED )
			LOGI("Service status = %s (0x%lx)", statenames[servstat.dwCurrentState], servstat.dwCurrentState);
	}

	// Now it has been created, we should start it as well.
	const int started = StartServiceA
	(
		service,
		0,		// num service arguments.
		NULL
	);
	if (started)
	{
		LOGI("turboledz service has been started.");
	}
	else
	{
		const DWORD err = GetLastError();
		LOGI("Failed to start service. Error: 0x%lx", err);
		return 5;
	}

	CloseServiceHandle(service);
	CloseServiceHandle(scm);
	return 0;
}

