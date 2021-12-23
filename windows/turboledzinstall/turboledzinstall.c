#include <windows.h>
#include <stdio.h>
#include <assert.h>

int main( int argc, char* argv[] )
{
	const int doremove = (argc > 1 && !strcmp(argv[1], "/Uninstall")) ? 1 : 0;

	char dirname[1024];
	memset(dirname, 0, sizeof(dirname));
	const char* s = strstr(argv[0], "turboledzinstall.exe");
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
		fprintf(stderr, "Did not find turboledzservice executable at %s\n", servicepath);
		fprintf(stderr, "Aborting installation.\n");
		return 1;
	}
	assert(f);
	fprintf(stderr, "Found executable at %s\n", servicepath);
	fclose(f);


	SC_HANDLE scm = OpenSCManagerA
	(
		NULL, // Machine name
		NULL, // Database name
		SC_MANAGER_ALL_ACCESS
	);

	if (!scm)
	{
		fprintf(stderr, "Failed to open Service Configuration Manager.\n");
		fprintf(stderr, "Aborting.\n");
		return 2;
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
			fprintf(stderr, "Failed to open service.\n");
			fprintf(stderr, "Aborting.\n");
			return 3;
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
		SC_MANAGER_ALL_ACCESS,
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
	if (!service)
	{
		DWORD err = GetLastError();
		fprintf(stderr, "Failed to create service. Error: %lx\n", err);
		return 4;
	}
	CloseServiceHandle(service);
	CloseServiceHandle(scm);
	return 0;
}

