#include <string.h>

#include "engine/core/file.h"
#include "engine/storage/digest.h"
#include "extract/source/packages.h"

static const char retail_cabinet_digest[] =
	"b9644797e15b742779d4e96182208f5b418309c0b20b8130f74ef6897281d65a";

/* Supported package hashes and offsets. Names are display IDs. */
static const struct mh_demo_package demo_packages[] = {
	{{"motordemo2_3dfx_intro", MH_DEMO_TRACK_GLIDE},
		"d1b4b725f6d98203517f707735abd8e4543325637756e6ea318319c490acdad9",
		36684042UL, 21661UL, 21432UL, MH_PACKAGE_CABINET},
	{{"motordemo2_soft", MH_DEMO_TRACK_SOFTWARE_SECOND},
		"4fed98a4e43cba36cca8340583d7515c7c5267299bf4756031315e804f477cef",
		8141384UL, 21651UL, 21432UL, MH_PACKAGE_CABINET},
	{{"motordemo_3dfx", MH_DEMO_TRACK_GLIDE},
		"aaf5a3bef2b4bb7bda493faabcb3ce631b9f3fb49e20f45a60a693ea7f25b755",
		10696533UL, 21661UL, 21432UL, MH_PACKAGE_CABINET},
	{{"motordemo_3dfx_intro", MH_DEMO_TRACK_GLIDE},
		"963bd78cdc7d34654368c571ed019d30d03b4c28f5286a95e0331604cbf17228",
		32978444UL, 21661UL, 21432UL, MH_PACKAGE_CABINET},
	{{"motordemo_mplayer", MH_DEMO_TRACK_MPLAYER},
		"859d8b22718074fca1f632a67461e95a303b385206669ff42f6bd318c3f38df6",
		13652609UL, 44585UL, 44272UL, MH_PACKAGE_ZIP_CABINET},
	{{"motordemo_soft", MH_DEMO_TRACK_SOFTWARE_FIRST},
		"5d3c7e4fef2eab1b10f8aa5045b3aed24ad002db6398c91ce968ab3137b8cffc",
		7971033UL, 21669UL, 21432UL, MH_PACKAGE_CABINET},
	{{"motordemo_soft_intro", MH_DEMO_TRACK_SOFTWARE_FIRST},
		"beadc997a062441d63e18813e58f87fd829fefa72df5cd903bf76df64e0a9360",
		30252911UL, 21665UL, 21432UL, MH_PACKAGE_CABINET}};

/* Supported retail executable and patch identities. */
static const struct mh_patch_package patch_packages[] = {
	{"3.0", "3dce2edb9fdbeba76ee66616a65d93b19a7cffe93bf473fb05b12ac6e14fc5f4",
		14025656UL, 99474UL, 13926182UL, MH_PATCH_CUMULATIVE},
	{"1.3", "7010339f0d0545799a6dc408bd95df67326f8b8773430fcdb39b458ca5ffd7e9",
		1680795UL, 0, 0, MH_PATCH_EARLIER},
	{"1.4", "6a7509f2693d8f7ed29d5db1f36949ae9eda43cb169b2861a66eb8185490c59b",
		2800387UL, 0, 0, MH_PATCH_EARLIER},
	{"2.1", "4dcc793eca0ee9037ad7dcdd995c359009d702882ea3fbb36d60788a8275435c",
		9178986UL, 0, 0, MH_PATCH_EARLIER},
	{"2.2", "98ad8be28a2866f4f909196df1a5579677fbbb4483adc01ed479a40278bd0eec",
		9173792UL, 0, 0, MH_PATCH_EARLIER},
	{"3.1-euro",
		"1b8fca2a36815d0aacc6ca40eddae4450d6b279033a6a2a83c0e227931adb58e",
		436951UL, 0, 0, MH_PATCH_EXECUTABLE_ONLY},
	{"3.1-creative",
		"ef6943a01c8e29127f6f0b1cc6e6ee2b9198255ea05aefb5d994e6ef91f6fc78",
		436937UL, 0, 0, MH_PATCH_EXECUTABLE_ONLY}};

const struct mh_patch_package *
mh_patch_package_supported(void)
{
	return &patch_packages[0];
}

const struct mh_patch_package *
mh_patch_package_find(unsigned long size, const char *sha256)
{
	size_t i;

	if (sha256 == NULL)
		return NULL;

	for (i = 0; i < sizeof(patch_packages) / sizeof(patch_packages[0]); i++)
		if (size == patch_packages[i].size &&
			strcmp(sha256, patch_packages[i].sha256) == 0)
			return &patch_packages[i];

	return NULL;
}

const struct mh_demo_package *
mh_demo_package_find(unsigned long size, const char *sha256)
{
	size_t i;

	if (sha256 == NULL)
		return NULL;

	for (i = 0; i < sizeof(demo_packages) / sizeof(demo_packages[0]); i++) {
		if (size == demo_packages[i].size &&
			strcmp(sha256, demo_packages[i].sha256) == 0)
			return &demo_packages[i];
	}

	return NULL;
}

enum mh_error_code
mh_demo_package_identify(const struct mh_resource_source *source,
	const char *path, const struct mh_demo_package **output,
	struct mh_error *error_out)
{
	const struct mh_demo_package *package;
	char digest[MH_SHA256_TEXT_SIZE];
	unsigned long size;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing package output");

	status = mh_resource_sha256(source, path, digest, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	package = mh_demo_package_find(size, digest);

	if (package == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"source is not one of the supplied demo packages");

	*output = package;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Verify the European retail cabinet before decoding its contents. */
enum mh_error_code
mh_retail_cabinet_verify(const struct mh_file *file, struct mh_error *error_out)
{
	struct mh_sha256 hash;
	unsigned char digest[MH_SHA256_BYTES];
	char text[MH_SHA256_TEXT_SIZE];

	if (file == NULL || file->data == NULL ||
		file->size != MH_RETAIL_CABINET_BYTES)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unexpected retail cabinet size");

	mh_sha256_init(&hash);
	mh_sha256_update(&hash, file->data, file->size);
	mh_sha256_finish(&hash, digest);
	mh_sha256_text(digest, text);

	if (strcmp(text, retail_cabinet_digest) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unrecognized retail cabinet; expected the supplied European disc");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
