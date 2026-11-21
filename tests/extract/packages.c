#include <stdio.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/storage/digest.h"
#include "extract/source/packages.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
check_recipes(void)
{
	const struct mh_demo_package *package;
	const char *digest;

	digest =
		"859d8b22718074fca1f632a67461e95a303b385206669ff42f6bd318c3f38df6";
	package = mh_demo_package_find(13652609UL, digest);
	check(package != NULL &&
			strcmp(package->recipe.id, "motordemo_mplayer") == 0 &&
			package->cabinet_offset == 44585UL &&
			package->container == MH_PACKAGE_ZIP_CABINET,
		"MPlayer recipe selects its independent ZIP member");
	check(mh_demo_package_find(13652608UL, digest) == NULL,
		"size mismatch does not identify a package");
	check(mh_demo_package_find(13652609UL, "unknown") == NULL,
		"digest mismatch does not identify a package");
	check(mh_demo_package_find(13652609UL, NULL) == NULL,
		"missing digest does not identify a package");
}

static void
check_patches(void)
{
	const struct mh_patch_package *patch;
	const char *digest;

	digest =
		"3dce2edb9fdbeba76ee66616a65d93b19a7cffe93bf473fb05b12ac6e14fc5f4";
	patch = mh_patch_package_find(14025656UL, digest);
	check(patch != NULL && patch == mh_patch_package_supported() &&
			patch->kind == MH_PATCH_CUMULATIVE &&
			patch->cabinet_offset == 99474UL &&
			patch->cabinet_size == 13926182UL,
		"Patch 3.0 selects the supported cumulative cabinet");
	check(mh_patch_package_find(14025655UL, digest) == NULL &&
			mh_patch_package_find(14025656UL, "unknown") == NULL &&
			mh_patch_package_find(14025656UL, NULL) == NULL,
		"unrecognized patch identity has no recipe");
	patch = mh_patch_package_find(2800387UL,
		"6a7509f2693d8f7ed29d5db1f36949ae9eda43cb169b2861a66eb8185490c59b");
	check(patch != NULL && patch->kind == MH_PATCH_EARLIER,
		"older patch remains distinguishable from cumulative content");
	patch = mh_patch_package_find(436951UL,
		"1b8fca2a36815d0aacc6ca40eddae4450d6b279033a6a2a83c0e227931adb58e");
	check(patch != NULL && patch->kind == MH_PATCH_EXECUTABLE_ONLY,
		"3.1 remains an executable-only update");
}

static void
check_resources(void)
{
	unsigned char bytes[16385];
	struct mh_resource_entry entry;
	const struct mh_resource_source *source;
	const struct mh_demo_package *package, *sentinel;
	struct mh_error error;
	char digest[MH_SHA256_TEXT_SIZE];
	unsigned long size;
	size_t i;

	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (unsigned char)(i % 251);

	entry.path = "renamed.exe";
	entry.data = bytes;
	entry.size = sizeof(bytes);
	check(mh_memory_resources_open(&entry, 1, &source, &error) ==
			MH_ERROR_NONE,
		"open memory source");

	if (failures != 0)
		return;

	check(mh_resource_sha256(source, entry.path, digest, &size, &error) ==
				MH_ERROR_NONE &&
			error.message == NULL && size == sizeof(bytes) &&
			strcmp(digest,
				"ba4f9b37402df1e3ad948a794ab43a9ed887d63e3a389c208ca4314fdd5add58") ==
				0,
		"selected provider hashes across read-chunk boundary");
	strcpy(digest, "unchanged");
	size = 17;
	check(mh_resource_sha256(source, "missing", digest, &size, &error) ==
				MH_ERROR_NOT_FOUND &&
			size == 17 && strcmp(digest, "unchanged") == 0,
		"failed hash preserves both outputs");
	sentinel = mh_demo_package_find(13652609UL,
		"859d8b22718074fca1f632a67461e95a303b385206669ff42f6bd318c3f38df6");
	package = sentinel;
	check(mh_demo_package_identify(source, entry.path, &package, &error) ==
				MH_ERROR_UNSUPPORTED &&
			package == sentinel,
		"unrecognized source bytes preserve selected package");
	mh_memory_resources_close(source);
}

int
main(void)
{
	check_recipes();
	check_patches();
	check_resources();

	return failures != 0;
}
