#ifndef MH_FONT_SOURCE_H
#define MH_FONT_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/font/glyphs.h"

struct mh_font_source_provider;
struct mh_resource_source;

/*
 * Load a logical font reference. The provider owns physical path/format
 * choices. Provider/resources/root/reference are borrowed for this call only.
 * Output is unchanged on failure. Success transfers malloc-owned image.rgba or
 * bytes; caller frees that buffer. No encoded input or parser state survives.
 * Diagnostic text belongs to the provider until its next operation.
 */
enum mh_error_code	mh_font_load(
	const struct mh_font_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_font *output,
	struct mh_error *error_out);

/*
 * Original software text needs byte-level coverage and foreground selection.
 * Alternate formats must preserve these operations/units, not reduce them to
 * a single alpha value. The result is a decoded map, not an encoded program.
 */
enum mh_error_code	mh_font_load_program(
	const struct mh_font_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_font_program *output,
	struct mh_error *error_out);

#endif
