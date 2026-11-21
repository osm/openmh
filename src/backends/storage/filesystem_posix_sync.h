#ifndef MH_FILESYSTEM_POSIX_SYNC_H
#define MH_FILESYSTEM_POSIX_SYNC_H

struct mh_directory_sync;

/*
 * Reserve a directory handle before changing a name. Once published, the
 * mutation is final in the local view. COMMIT retries any failed sync.
 */
#ifdef __EMSCRIPTEN__
/* IndexedDB commits the virtual filesystem through the browser host. */
#define mh_directory_sync_parent(path, pending) (*(pending) = 0, 0)
#define mh_directory_sync_hold(fd, pending) (*(pending) = 0, 0)
#define mh_directory_sync_finish(pending, published) ((void)(pending))
#else
int	mh_directory_sync_parent(const char *path,
	struct mh_directory_sync **pending);
int	mh_directory_sync_hold(int directory,
	struct mh_directory_sync **pending);
void	mh_directory_sync_finish(struct mh_directory_sync *pending,
	int published);
int	mh_directory_sync_retry(void);
#endif

#endif
