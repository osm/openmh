#include <float.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/software_order.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
setup_guards(void)
{
	struct mh_software_order root, child, saved;
	struct mh_software_point points[4];
	struct mh_error error;
	unsigned int bucket, i;

	memset(&root, 0, sizeof(root));
	memset(&child, 0, sizeof(child));
	memset(points, 0, sizeof(points));
	CHECK(mh_software_order_setup(1, 11, MH_SOFTWARE_ORDER_ROOT, &root,
		&error) == MH_ERROR_NONE &&
		error.message == NULL);
	memcpy(&saved, &root, sizeof(saved));
	CHECK(mh_software_order_setup(1, 11, MH_SOFTWARE_ORDER_CHILD, &child,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&saved, &root, sizeof(root)) == 0);
	CHECK(root.buckets == MH_SOFTWARE_BUCKETS && root.near_distance == 1);
	CHECK(root.bands[0][MH_SOFTWARE_ORDER_PLAIN].scale == 0);
	CHECK(root.bands[0][MH_SOFTWARE_ORDER_PLAIN].offset == .5f);
	CHECK(memcmp(root.bands[1], child.bands[1], sizeof(root.bands[1])) ==
		0);

	for (i = 0; i < 4; i++)
		points[i].value[2] = 4;

	CHECK(mh_software_order_bucket(&root, points, 4, 0, 0, &bucket, NULL) ==
			MH_ERROR_NONE &&
		bucket == 2047);
	CHECK(mh_software_order_bucket(&child, points, 4, 0, 0, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 2006);
	CHECK(mh_software_order_bucket(&child, points, 4, 0, 132, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 2006);
	CHECK(mh_software_order_setup(0, 11, MH_SOFTWARE_ORDER_ROOT, &root,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_order_setup(11, 11, MH_SOFTWARE_ORDER_ROOT, &root,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_order_setup(11, 1, MH_SOFTWARE_ORDER_ROOT, &root,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_order_setup(1, 11, (enum mh_software_order_model)3,
		&root, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_order_setup(1e-9f, 2e-9f, MH_SOFTWARE_ORDER_ROOT,
		&root, NULL) == MH_ERROR_RANGE);
	CHECK(mh_software_order_setup(FLT_MIN, FLT_MIN * 1.125f,
		MH_SOFTWARE_ORDER_ROOT, &root, NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&saved, &root, sizeof(root)) == 0);
	CHECK(mh_software_order_setup(1, 11, MH_SOFTWARE_ORDER_ROOT, NULL,
		NULL) == MH_ERROR_INVALID);

	return 0;
}

static int
bucket_guards(void)
{
	struct mh_software_order order;
	struct mh_software_point points[4];
	struct mh_error error;
	unsigned int i, bucket;

	memset(&order, 0, sizeof(order));
	memset(points, 0, sizeof(points));
	order.buckets = MH_SOFTWARE_BUCKETS;
	order.near_distance = 1;
	order.inverse_range = 0.25f;
	order.bands[0][MH_SOFTWARE_ORDER_PLAIN].scale = 1;

	for (i = 0; i < 4; i++)
		points[i].value[2] = 3;

	CHECK(mh_software_order_bucket(&order, points, 4, 0, 0, &bucket,
		&error) == MH_ERROR_NONE &&
		bucket == 2047);
	points[0].outcode = MH_CLIP_NEAR;
	points[0].value[2] = -1000;
	CHECK(mh_software_order_bucket(&order, points, 4, 0, 0, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 2559);
	order.bands[0][MH_SOFTWARE_ORDER_FLAG4].offset = 0.25f;
	order.bands[0][MH_SOFTWARE_ORDER_FLAG128].offset = 0.75f;
	CHECK(mh_software_order_bucket(&order, points, 3, 0, 132, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 1023);
	CHECK(mh_software_order_bucket(&order, points, 3, 0, 128, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 3071);
	order.bands[1][MH_SOFTWARE_ORDER_PLAIN].offset = 1;
	CHECK(mh_software_order_bucket(&order, points, 3, 1, 0, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 4095);
	CHECK(mh_software_order_bucket(&order, points, 3, 7, 128, &bucket,
		NULL) == MH_ERROR_NONE &&
		bucket == 3071);

	bucket = 123;
	points[1].outcode = MH_CLIP_FAR;
	CHECK(mh_software_order_bucket(&order, points, 3, 0, 0, &bucket,
		&error) == MH_ERROR_INVALID &&
		bucket == 123);
	points[1].outcode = 0;
	order.bands[0][MH_SOFTWARE_ORDER_PLAIN].offset = 10;
	CHECK(mh_software_order_bucket(&order, points, 3, 0, 0, &bucket,
		NULL) == MH_ERROR_RANGE &&
		bucket == 123);
	order.buckets = MH_SOFTWARE_BUCKETS + 1;
	CHECK(mh_software_order_bucket(&order, points, 3, 0, 0, &bucket,
		NULL) == MH_ERROR_INVALID &&
		bucket == 123);
	CHECK(mh_software_order_bucket(NULL, points, 3, 0, 0, &bucket, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_order_bucket(&order, NULL, 3, 0, 0, &bucket, NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

static int
sort_guards(void)
{
	struct mh_software_order_span spans[4] = {
		{0, 2, 3}, {2, 4, 1}, {6, 1, 3}, {7, 2, 0}};

	struct mh_software_order_span saved[4];

	CHECK(mh_software_order_sort(NULL, 0, 0, 4, NULL) == MH_ERROR_NONE);
	memcpy(saved, spans, sizeof(spans));
	CHECK(mh_software_order_sort(spans, 4, 8, 4, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(saved, spans, sizeof(spans)) == 0);
	CHECK(mh_software_order_sort(spans, 4, 9, 3, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(saved, spans, sizeof(spans)) == 0);
	spans[3].first = 6;
	CHECK(mh_software_order_sort(spans, 4, 9, 4, NULL) == MH_ERROR_INVALID);
	memcpy(spans, saved, sizeof(spans));
	CHECK(mh_software_order_sort(spans, 4, 9, 4, NULL) == MH_ERROR_NONE);
	CHECK(spans[0].first == 7 && spans[0].count == 2);
	CHECK(spans[1].first == 2 && spans[1].count == 4);
	CHECK(spans[2].first == 6 && spans[2].count == 1);
	CHECK(spans[3].first == 0 && spans[3].count == 2);
	spans[0].first = (size_t)-1;
	CHECK(mh_software_order_sort(spans, 1, (size_t)-1, 4, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_order_sort(NULL, 1, 0, 4, NULL) == MH_ERROR_INVALID);

	return 0;
}

int
main(void)
{

	CHECK(setup_guards() == 0);
	CHECK(bucket_guards() == 0);
	CHECK(sort_guards() == 0);
	puts("software bucket selection and whole-face ordering guards passed");

	return 0;
}
