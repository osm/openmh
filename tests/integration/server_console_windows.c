#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "server/hosts/console.h"

#define TIMEOUT_MS 5000
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "Windows console line %d: %s\n",       \
				__LINE__, #condition);                         \
			exit(1);                                               \
		}                                                              \
	} while (0)

static PHANDLER_ROUTINE handler;
extern	BOOL(
	WINAPI *__real___imp_SetConsoleCtrlHandler)( PHANDLER_ROUTINE routine,
	BOOL add);

static BOOL WINAPI
register_handler(PHANDLER_ROUTINE routine, BOOL add)
{
	if (add)
		handler = routine;

	return __real___imp_SetConsoleCtrlHandler(routine, add);
}

BOOL(WINAPI *__wrap___imp_SetConsoleCtrlHandler)(
	PHANDLER_ROUTINE routine, BOOL add) = register_handler;

static void
check_pipe(void)
{
	HANDLE original, input, output;
	char bytes[32];
	DWORD written;
	size_t size;
	int eof;

	original = GetStdHandle(STD_INPUT_HANDLE);
	CHECK(CreatePipe(&input, &output, NULL, 0));
	CHECK(SetStdHandle(STD_INPUT_HANDLE, input));
	CHECK(mh_server_console_read(bytes, sizeof(bytes), &size, &eof, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 0 && !eof);
	CHECK(WriteFile(output, "status\r\nquit\n", 13, &written, NULL));
	CHECK(written == 13);
	CHECK(mh_server_console_read(bytes, sizeof(bytes), &size, &eof, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 13 && !eof && memcmp(bytes, "status\r\nquit\n", 13) == 0);
	CloseHandle(output);
	CHECK(mh_server_console_read(bytes, sizeof(bytes), &size, &eof, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 0 && eof && !mh_server_console_stopped());
	CHECK(SetStdHandle(STD_INPUT_HANDLE, original));
	CloseHandle(input);
}

static DWORD WINAPI
close_console(void *context)
{
	(void)context;

	return handler(CTRL_CLOSE_EVENT);
}

static void
check_shutdown(void)
{
	HANDLE worker;
	DWORD code;
	unsigned int waited;

	CHECK(mh_server_console_acquire(NULL) == MH_ERROR_NONE);
	CHECK(!mh_server_console_stopped());
	CHECK(handler(CTRL_C_EVENT));
	CHECK(mh_server_console_stopped());
	mh_server_console_release();
	CHECK(mh_server_console_acquire(NULL) == MH_ERROR_NONE);
	CHECK(!mh_server_console_stopped());
	worker = CreateThread(NULL, 0, close_console, NULL, 0, NULL);
	CHECK(worker != NULL);

	for (waited = 0; waited < TIMEOUT_MS && !mh_server_console_stopped();
		waited++)
		Sleep(1);

	CHECK(mh_server_console_stopped());
	CHECK(WaitForSingleObject(worker, 0) == WAIT_TIMEOUT);
	mh_server_console_release();
	CHECK(WaitForSingleObject(worker, TIMEOUT_MS) == WAIT_OBJECT_0);
	CHECK(GetExitCodeThread(worker, &code) && code == TRUE);
	CloseHandle(worker);
}

int
main(void)
{
	CHECK(mh_server_console_acquire(NULL) == MH_ERROR_NONE);
	check_pipe();
	mh_server_console_release();
	check_shutdown();
	puts("Windows server console: native pipe polling/EOF and simulated control-handler shutdown handoff passed");

	return 0;
}
