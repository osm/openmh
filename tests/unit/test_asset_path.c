#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_path.h"
#include "engine/core/error.h"
#include "engine/storage/asset_path.h"

int
main(int argc, char *argv[])
{
	char path[1024];
	char small[8];
	enum mh_error_code status;
	int found;

	if (mh_path_directory_length("disc.cue") != 0 ||
		mh_path_directory_length("C:\\Games\\disc.cue") !=
			strlen("C:\\Games\\") ||
		mh_path_directory_length("C:\\Games/mixed\\server.cfg") !=
			strlen("C:\\Games/mixed\\") ||
		mh_path_directory_length("/server.cfg") != 1)
		return 1;

	if (argc != 2)
		return 1;

	status = mh_asset_path(
		argv[1], "cArS\\TeSt.MYO", path, sizeof(path), NULL);

	if (status != MH_ERROR_NONE || strstr(path, "/Cars/test.myo") == NULL) {
		fprintf(stderr, "case/separator resolution failed\n");
		return 1;
	}

	if (mh_asset_path(argv[1], "Cars/missing", path, sizeof(path), NULL) ==
			MH_ERROR_NONE ||
		mh_asset_path(argv[1], "../Cars", path, sizeof(path), NULL) ==
			MH_ERROR_NONE ||
		mh_asset_path(argv[1], "/Cars", path, sizeof(path), NULL) ==
			MH_ERROR_NONE ||
		mh_asset_path(argv[1], "C:\\Cars", path, sizeof(path), NULL) ==
			MH_ERROR_NONE ||
		mh_asset_path(argv[1], "Cars/./test.myo", path, sizeof(path),
			NULL) == MH_ERROR_NONE ||
		mh_asset_path(argv[1], "Cars/test.myo", small, sizeof(small),
			NULL) == MH_ERROR_NONE) {
		fprintf(stderr, "invalid/oversized path was accepted\n");
		return 1;
	}

	if (mh_asset_path_find(argv[1], "Cars/missing", path, sizeof(path),
		&found, NULL) != MH_ERROR_NONE ||
		found ||
		mh_asset_path_find(argv[1], "Cars/test.myo", path, sizeof(path),
			&found, NULL) != MH_ERROR_NONE ||
		!found ||
		mh_asset_path_find(argv[1], "Cars/../test.myo", path,
			sizeof(path), &found, NULL) == MH_ERROR_NONE ||
		mh_asset_path_find(argv[1], "Missing/../test.myo", path,
			sizeof(path), &found, NULL) == MH_ERROR_NONE ||
		mh_asset_path_find(argv[1], "Cars/test.myo", small,
			sizeof(small), &found, NULL) == MH_ERROR_NONE) {
		fprintf(stderr,
			"optional asset lookup confused absence with errors\n");
		return 1;
	}

	puts("DOS asset paths resolve using actual directory entry spelling");

	return 0;
}
