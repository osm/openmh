#ifndef MH_SETTINGS_ADDRESS_BOOK_H
#define MH_SETTINGS_ADDRESS_BOOK_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_ADDRESS_LIMIT 4096
#define MH_ADDRESS_TEXT_CAPACITY 512
#define MH_ADDRESS_NAME_BYTES 32
#define MH_ADDRESS_IP_BYTES 128
#define MH_ADDRESS_FILENAME_BYTES 256

struct mh_address_profile {
	char	name[MH_ADDRESS_NAME_BYTES];
	char	ip[MH_ADDRESS_IP_BYTES];
	int	tcp;
};

struct mh_address_entry {
	struct mh_address_profile	profile;
	char				filename[MH_ADDRESS_FILENAME_BYTES];
};

/* Source names and bytes are borrowed for the duration of read(). */
struct mh_address_source {
	const char		*filename;
	const unsigned char	*text;
	size_t			size;
};

/* Owns entries until mh_address_book_free(). */
struct mh_address_book {
	struct mh_address_entry	*entries;
	unsigned int		count;
};

/*
 * TCP/IP fields of the NPC reader. A non-TCP file succeeds with tcp=0 and must
 * be omitted by callers. Description joins tokens and keeps the first 31 bytes.
 * Address takes one token, only after Type TCP. Missing description becomes
 * TCP/IP_%02u. Ordinal is the catalog index, 2..4096. IP copies are bounded to
 * 127 bytes. The shared legacy byte lexer handles comments; there is no
 * quoting. Errors preserve output. No legacy transport settings are
 * implemented.
 */
enum mh_error_code	mh_address_profile_read(const unsigned char *text,
	size_t size, unsigned int ordinal, struct mh_address_profile *output,
	struct mh_error *error_out);

/*
 * Canonical TCP NPC text, with unquoted values. Empty, space and
 * comment-bearing text retains writer/reload semantics. No control bytes. NULL
 * text/capacity 0 queries size; all errors preserve output and size.
 */
enum mh_error_code	mh_address_profile_encode(
	const struct mh_address_profile *profile, char *text, size_t capacity,
	size_t *size, struct mh_error *error_out);

/*
 * Build a basename: strip controls, bytes >127 and :;<>?*\./, use ! if empty,
 * lowercase ASCII letters, add .npc and insert ! before the extension until
 * unique under ASCII case folding. Names <=31 bytes, existing names <=255
 * bytes. Bound collision growth to avoid unbounded names.
 */
enum mh_error_code	mh_address_filename(const char *name,
	const char *const *existing, unsigned int count,
	char output[MH_ADDRESS_FILENAME_BYTES], struct mh_error *error_out);

/*
 * Ordered sources, at most 4095. Filenames are unique case-insensitive NPC
 * basenames; NULL text/size 0 represents an unreadable source, omitted.
 * Native catalog contains only TCP entries, with one protected TCP/IP
 * local-search entry at index 0 (empty IP/filename). Source ordinal retains
 * the original two built-ins for fallback names. Output must be empty.
 * Add/remove are memory operations; platform storage commits disk changes.
 */
enum mh_error_code	mh_address_book_read(
	const struct mh_address_source *sources, unsigned int count,
	struct mh_address_book *output, struct mh_error *error_out);
enum mh_error_code	mh_address_book_add(struct mh_address_book *book,
	const char *name, const char *ip, struct mh_error *error_out);
enum mh_error_code	mh_address_book_remove(struct mh_address_book *book,
	unsigned int index, struct mh_error *error_out);

/*
 * Stable unsigned-byte order over all 32 name bytes, keeping the built-in entry
 * first. Takes a valid catalog; allocates nothing. Optional previous[] has
 * count elements and maps each new index to its old index, for owners with
 * associated state such as deletion identities.
 */
void	mh_address_book_sort(struct mh_address_book *book,
	unsigned int *previous);
void	mh_address_book_free(struct mh_address_book *book);

#endif
