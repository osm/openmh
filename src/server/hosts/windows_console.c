#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "engine/core/charset.h"
#include "server/hosts/console.h"

#define CLOSE_WAIT_MS 5000
#define CLOSE_POLL_MS 10

static volatile LONG stopped, finished;
static WCHAR pending_character;
static unsigned char pending_byte;
static unsigned int pending_repeats;

static BOOL WINAPI
stop_control(DWORD event)
{
	unsigned int waited;

	if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT &&
		event != CTRL_CLOSE_EVENT && event != CTRL_LOGOFF_EVENT &&
		event != CTRL_SHUTDOWN_EVENT)
		return FALSE;

	InterlockedExchange(&stopped, 1);

	if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT)
		return TRUE;

	/* Returning from a close handler permits Windows to terminate us. */
	for (waited = 0; waited < CLOSE_WAIT_MS; waited += CLOSE_POLL_MS) {
		if (InterlockedCompareExchange(&finished, 0, 0))
			break;

		Sleep(CLOSE_POLL_MS);
	}

	return TRUE;
}

enum mh_error_code
mh_server_console_acquire(struct mh_error *error_out)
{
	InterlockedExchange(&stopped, 0);
	InterlockedExchange(&finished, 0);
	pending_repeats = 0;

	if (!SetConsoleCtrlHandler(stop_control, TRUE))
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot install server console shutdown handler");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_server_console_release(void)
{
	InterlockedExchange(&finished, 1);
	SetConsoleCtrlHandler(stop_control, FALSE);
}

int
mh_server_console_stopped(void)
{
	return InterlockedCompareExchange(&stopped, 0, 0) != 0;
}

static void
echo_character(WCHAR character)
{
	HANDLE output;
	DWORD written;
	const WCHAR *text;
	DWORD size;

	output = GetStdHandle(STD_OUTPUT_HANDLE);
	text = &character;
	size = 1;

	if (character == L'\r') {
		text = L"\r\n";
		size = 2;
	} else if (character == L'\b') {
		text = L"\b \b";
		size = 3;
	}

	(void)WriteConsoleW(output, text, size, &written, NULL);
}

static enum mh_error_code
read_console(HANDLE input, char *bytes, size_t capacity, size_t *size,
	struct mh_error *error_out)
{
	INPUT_RECORD event;
	DWORD available, count;
	size_t polled;

	for (polled = 0; polled < capacity && *size < capacity; polled++) {
		if (pending_repeats == 0) {
			if (!GetNumberOfConsoleInputEvents(input, &available))
				return mh_error_set(error_out, MH_ERROR_IO,
					"cannot poll server console input");

			if (available == 0)
				break;

			if (!ReadConsoleInputW(input, &event, 1, &count))
				return mh_error_set(error_out, MH_ERROR_IO,
					"cannot read server console input");

			if (count == 0 || event.EventType != KEY_EVENT ||
				!event.Event.KeyEvent.bKeyDown)
				continue;

			pending_character =
				event.Event.KeyEvent.uChar.UnicodeChar;

			/*
			 * Server commands and driver names retain legacy byte
			 * text.
			 */
			pending_byte =
				mh_windows_1252_character(pending_character);

			if (pending_byte == 0)
				continue;

			pending_repeats = event.Event.KeyEvent.wRepeatCount;

			if (pending_repeats == 0)
				continue;
		}

		bytes[(*size)++] = (char)pending_byte;
		echo_character(pending_character);
		pending_repeats--;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_console_read(char *bytes, size_t capacity, size_t *size, int *eof,
	struct mh_error *error_out)
{
	HANDLE input;
	DWORD mode, kind, available, count;

	*size = 0;
	*eof = 0;
	input = GetStdHandle(STD_INPUT_HANDLE);

	if (input == NULL || input == INVALID_HANDLE_VALUE) {
		*eof = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (GetConsoleMode(input, &mode))
		return read_console(input, bytes, capacity, size, error_out);

	kind = GetFileType(input);

	if (kind == FILE_TYPE_PIPE) {
		if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL)) {
			if (GetLastError() == ERROR_BROKEN_PIPE) {
				*eof = 1;
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
			}

			return mh_error_set(error_out, MH_ERROR_IO,
				"cannot poll server input pipe");
		}

		if (available == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (capacity > available)
			capacity = available;
	} else if (kind != FILE_TYPE_DISK) {
		*eof = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!ReadFile(input, bytes, (DWORD)capacity, &count, NULL)) {
		if (GetLastError() != ERROR_BROKEN_PIPE)
			return mh_error_set(error_out, MH_ERROR_IO,
				"cannot read server redirected input");

		count = 0;
	}

	*size = count;
	*eof = count == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
