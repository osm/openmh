#ifndef MH_ZIP_WRITE_H
#define MH_ZIP_WRITE_H

#include "interfaces/storage/data_stream.h"
#include "interfaces/storage/resource.h"

struct mh_zip_writer;

enum mh_zip_compression {
	MH_ZIP_STORE,
	MH_ZIP_COMPRESS
};

/*
 * Deterministic classic ZIP; compression keeps entries stored if not smaller.
 * Entries over the 32 MiB compression workspace budget also remain stored.
 * Borrows the empty private stream.
 */
enum mh_error_code	mh_zip_writer_open(struct mh_data_stream *stream,
	struct mh_zip_writer **output, struct mh_error *error_out);

/* Names are relative slash paths. Directory entries have no source bytes. */
enum mh_error_code	mh_zip_writer_add(struct mh_zip_writer *writer,
	const struct mh_resource_source *source, const char *path,
	const char *name, int directory, enum mh_zip_compression compression,
	struct mh_error *error_out);

/* Write the index; the caller still owns sealing and publishing the stream. */
enum mh_error_code	mh_zip_writer_finish(struct mh_zip_writer *writer,
	struct mh_error *error_out);
void			mh_zip_writer_close(struct mh_zip_writer *writer);

#endif
