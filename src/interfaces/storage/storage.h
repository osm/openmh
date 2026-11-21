#ifndef MH_INTERFACES_STORAGE_STORAGE_H
#define MH_INTERFACES_STORAGE_STORAGE_H

#include "engine/core/error.h"

/* Readiness/durability are distinct from local file-format transactions. */
enum mh_storage_operation {
	MH_STORAGE_PREPARE,
	MH_STORAGE_COMMIT
};

enum mh_storage_status {
	MH_STORAGE_FAILED = -1,
	MH_STORAGE_READY,
	MH_STORAGE_PENDING
};

/*
 * PREPARE makes the resource namespace and the writable user-data view ready
 * before any application resource or settings access. COMMIT durably publishes
 * completed mutations of that view; no new game/menu steps run while pending.
 * The provider may return READY immediately when there is nothing to do.
 *
 * File stores still own validation, atomic local replacement and dirty
 * baselines. A cached backend must track uncommitted local mutations
 * independently of those baselines. Failed COMMIT leaves them available for
 * retry; retry must not replay game transactions. The provider must not modify
 * packaged resources.
 *
 * Callbacks never wait for external completion. poll advances a pending
 * request; a terminal result means no callback remains outstanding. cancel
 * synchronously detaches outstanding callbacks before the borrowed context can
 * be released. Error text is copied on failure. Ops/context remain live until
 * storage close.
 */
struct mh_storage_ops {
	enum mh_storage_status (*begin)(void *context,
		enum mh_storage_operation	operation, struct mh_error *error);
	enum mh_storage_status	(*poll)(void *context, struct mh_error *error);
	void					(*cancel)(void *context);
};

struct mh_storage;

#endif
