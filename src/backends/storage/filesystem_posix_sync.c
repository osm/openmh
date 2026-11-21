#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_posix_sync.h"

struct mh_directory_sync {
	int				fd;
	dev_t				device;
	ino_t				inode;
	struct mh_directory_sync	*next;
};

/* The filesystem provider is shared by native sessions in this process. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct mh_directory_sync *unsynced;

static int
reserve(int fd, struct mh_directory_sync **output)
{
	struct mh_directory_sync *sync;
	struct stat info;
	int saved;

	if (fd < 0)
		return -1;

	sync = malloc(sizeof(*sync));

	if (sync == NULL || fstat(fd, &info) != 0) {
		saved = errno;
		free(sync);
		close(fd);
		errno = saved;
		return -1;
	}

	sync->fd = fd;
	sync->device = info.st_dev;
	sync->inode = info.st_ino;
	sync->next = NULL;
	*output = sync;

	return 0;
}

int
mh_directory_sync_hold(int directory, struct mh_directory_sync **pending)
{
	return reserve(fcntl(directory, F_DUPFD_CLOEXEC, 0), pending);
}

int
mh_directory_sync_parent(const char *path, struct mh_directory_sync **pending)
{
	char *copy;
	int fd, saved;

	copy = strdup(path);

	if (copy == NULL)
		return -1;

	fd = open(dirname(copy), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	saved = errno;
	free(copy);
	errno = saved;

	return reserve(fd, pending);
}

static void
release(struct mh_directory_sync *sync)
{
	close(sync->fd);
	free(sync);
}

static int
flush(int fd)
{
	int result;

	do {
		result = fsync(fd);
	} while (result != 0 && errno == EINTR);

	return result;
}

void
mh_directory_sync_finish(struct mh_directory_sync *pending, int published)
{
	struct mh_directory_sync *entry;

	if (published && flush(pending->fd) != 0) {
		pthread_mutex_lock(&lock);

		for (entry = unsynced; entry != NULL; entry = entry->next) {
			if (entry->device == pending->device &&
				entry->inode == pending->inode)
				break;
		}

		if (entry == NULL) {
			pending->next = unsynced;
			unsynced = pending;
			pthread_mutex_unlock(&lock);
			return;
		}

		pthread_mutex_unlock(&lock);
	}

	release(pending);
}

int
mh_directory_sync_retry(void)
{
	struct mh_directory_sync *entry;
	int saved;

	saved = 0;
	pthread_mutex_lock(&lock);

	while (unsynced != NULL) {
		entry = unsynced;

		if (flush(entry->fd) != 0) {
			saved = errno;
			break;
		}

		unsynced = entry->next;
		release(entry);
	}

	pthread_mutex_unlock(&lock);

	if (saved == 0)
		return 0;

	errno = saved;

	return -1;
}
