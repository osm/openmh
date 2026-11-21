#ifndef MH_ASSETS_PDI_H
#define MH_ASSETS_PDI_H

#include <stddef.h>

#include "engine/core/error.h"

struct mh_pdi {
	const unsigned char	*data;
	unsigned long		capacity;
	unsigned long		count;
	size_t			bitmap_offset;
	size_t			directory_offset;
	size_t			payload_offset;
};

struct mh_pdi_entry {
	const char		*name;
	const unsigned char	*data;
	size_t			size;
};

/*
 * Borrows immutable file bytes; keep the buffer alive and unchanged while
 * using the parsed archive or its entries. Output is assigned only on success.
 */
enum mh_error_code	mh_pdi_parse(const unsigned char *data, size_t size,
	struct mh_pdi *archive, struct mh_error *error_out);

/*
 * Accessors require a successfully parsed archive. Entry uses a slot index,
 * not an ordinal; empty/out-of-range slots return zero. Failed lookups leave
 * the output unchanged.
 */
int	mh_pdi_entry(const struct mh_pdi *archive, unsigned long slot,
	struct mh_pdi_entry *entry);

/* The original texture names differ in ASCII case between MYO and PDI. */
int	mh_pdi_find(const struct mh_pdi *archive, const char *name,
	struct mh_pdi_entry *entry);

#endif
