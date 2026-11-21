#include "client/diagnostics/storage_demo_file_internal.h"
#include "client/diagnostics/storage_demo_recording.h"

static enum mh_error_code
finish_open(struct mh_data_directory *directory, enum mh_error_code status,
	struct mh_error *error_out)
{
	if (status == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	mh_data_directory_close(directory);

	return mh_error_set(error_out, status,
		status == MH_ERROR_EXISTS
			? "recording destination already exists"
			: "cannot open recording file");
}

enum mh_error_code
mh_demo_recording_open(const char *path, const struct mh_field *field,
	const struct mh_profile *profiles,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	const char *filename;
	enum mh_error_code status;

	directory = NULL;
	filename = NULL;
	status = mh_diagnostic_demo_directory(
		path, NULL, &directory, &filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_demo_recording_open_in(directory, filename, field, profiles,
		controls, output, error_out);

	return finish_open(directory, status, error_out);
}

enum mh_error_code
mh_demo_recording_open_ghost(const char *path, struct mh_demo_race *race,
	const struct mh_profile *human,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	const char *filename;
	enum mh_error_code status;

	directory = NULL;
	filename = NULL;
	status = mh_diagnostic_demo_directory(
		path, NULL, &directory, &filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_demo_recording_open_ghost_in(
		directory, filename, race, human, controls, output, error_out);

	return finish_open(directory, status, error_out);
}

enum mh_error_code
mh_demo_recording_open_network(const char *path, const char *asset_root,
	struct mh_net_race *race, const struct mh_net_session *session,
	struct mh_demo_recording **output, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	const char *filename;
	enum mh_error_code status;

	if (asset_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing original asset directory");

	directory = NULL;
	filename = NULL;
	status = mh_diagnostic_demo_directory(
		path, asset_root, &directory, &filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_demo_recording_open_network_in(
		directory, filename, race, session, output, error_out);

	return finish_open(directory, status, error_out);
}
