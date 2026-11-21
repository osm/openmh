#define _POSIX_C_SOURCE 200809L
#include <sys/resource.h>
#include <sys/wait.h>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/storage/filesystem_file_replace.h"

#define WRITE_CAPACITY 65536

static int
limited_write(const char *path, size_t size)
{
	unsigned char data[WRITE_CAPACITY];
	struct rlimit limit;
	pid_t child;
	int child_status;

	child = fork();

	if (child == -1)
		return 0;

	if (child == 0) {
		/*
		 * Force both buffered flush and larger write failures, in
		 * isolation.
		 */
		if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR ||
			getrlimit(RLIMIT_FSIZE, &limit) != 0)
			_exit(2);

		limit.rlim_cur = 1;

		if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
			_exit(3);

		memset(data, 'x', sizeof(data));
		_exit(mh_file_replace(path, data, size) ==
					MH_FILE_REPLACE_COMMIT
				? 0
				: 1);
	}

	return waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0;
}

static int
original_retained(const char *path)
{
	char data[32];
	FILE *stream;
	size_t size;
	int failed;

	stream = fopen(path, "rb");

	if (stream == NULL)
		return 0;

	size = fread(data, 1, sizeof(data), stream);
	failed = ferror(stream);

	if (fclose(stream) != 0)
		failed = 1;

	return !failed && size == 8 && memcmp(data, "old file", 8) == 0;
}

int
main(void)
{
	char *directory, path[128];
	FILE *stream;
	DIR *entries;
	struct dirent *entry;
	int success;

	directory = mh_test_directory("mh-file-replace-");

	if (directory == NULL)
		return 1;

	/* mh_test_directory's path is at most 95 bytes. */
	sprintf(path, "%s/saved", directory);
	stream = fopen(path, "wb");

	if (stream == NULL)
		return 1;

	success = fwrite("old file", 1, 8, stream) == 8;

	if (fclose(stream) != 0)
		success = 0;

	success = success && limited_write(path, 64) && original_retained(path);
	success = success && limited_write(path, WRITE_CAPACITY) &&
		original_retained(path);
	entries = opendir(directory);

	if (entries == NULL)
		success = 0;
	else {
		while ((entry = readdir(entries)) != NULL)
			if (strcmp(entry->d_name, ".") != 0 &&
				strcmp(entry->d_name, "..") != 0 &&
				strcmp(entry->d_name, "saved") != 0) {
				fprintf(stderr, "leftover temporary: %s\n",
					entry->d_name);
				success = 0;
			}

		if (closedir(entries) != 0)
			success = 0;
	}

	if (unlink(path) != 0 || rmdir(directory) != 0)
		success = 0;

	if (success)
		puts("File replacement: write/flush failures preserve destination and remove temporary files");
	else
		fputs("File replacement failure cleanup failed\n", stderr);

	return success ? 0 : 1;
}
