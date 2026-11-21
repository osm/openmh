#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "backends/window/sdl_provider.h"

#define CHILD_SUCCESS 73
#define CHILD_TIMEOUT 10000
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "Windows startup line %d: %s\n",       \
				__LINE__, #condition);                         \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_arguments(int argc, char *argv[])
{
	CHECK(argc == 5);
	CHECK(strcmp(argv[1], "--child") == 0);
	CHECK(strcmp(argv[2], "Motorhead \303\245\303\244\303\266") == 0);
	CHECK(strcmp(argv[3], "C:\\\346\270\270\346\210\217\\userdata") == 0);
	CHECK(argv[4][0] == 0);

	return CHILD_SUCCESS;
}

static int
run_test(int argc, char *argv[])
{
	WCHAR executable[4096], command[8192];
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;
	DWORD length, result, code;

	if (argc > 1)
		return check_arguments(argc, argv);

	length = GetModuleFileNameW(
		NULL, executable, sizeof(executable) / sizeof(*executable));
	CHECK(length > 0 && length < sizeof(executable) / sizeof(*executable));
	lstrcpyW(command, L"\"");
	lstrcatW(command, executable);
	lstrcatW(command,
		L"\" --child \"Motorhead \x00e5\x00e4\x00f6\" \"C:\\\x6e38\x620f\\userdata\" \"\"");
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	memset(&process, 0, sizeof(process));
	CHECK(CreateProcessW(executable, command, NULL, NULL, FALSE, 0, NULL,
		NULL, &startup, &process));
	result = WaitForSingleObject(process.hProcess, CHILD_TIMEOUT);

	if (result != WAIT_OBJECT_0) {
		TerminateProcess(process.hProcess, 1);
		WaitForSingleObject(process.hProcess, CHILD_TIMEOUT);
	}

	CHECK(GetExitCodeProcess(process.hProcess, &code));
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	CHECK(result == WAIT_OBJECT_0 && code == CHILD_SUCCESS);
	puts("Windows SDL startup: UTF-8 arguments, quoting, empty arguments and exit status passed");

	return 0;
}

int
main(int argc, char *argv[])
{
	return mh_sdl_run_application(argc, argv, run_test);
}
