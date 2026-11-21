#ifndef MH_MENU_CATALOG_H
#define MH_MENU_CATALOG_H

#include "engine/core/error.h"
/*
 * Stable ascending unsigned Division order. Returns original source indices,
 * without moving caller-owned assets. Counts 1..4096, keys u32. Equal keys
 * preserve incoming enumeration order. Atomic failure.
 */
enum mh_error_code	mh_menu_catalog_order(const unsigned long *divisions,
	unsigned int count, unsigned int *order, unsigned int capacity,
	struct mh_error *error_out);

/*
 * Restore by exact name after sorting, last match wins; missing names select
 * zero. Names contain <=255 bytes. Atomic failure.
 */
enum mh_error_code	mh_menu_catalog_restore(const char *const *names,
	unsigned int count, const char *saved, unsigned int *selected,
	struct mh_error *error_out);

#endif
