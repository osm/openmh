#ifndef MH_TEST_COLLISION_FIXTURE_H
#define MH_TEST_COLLISION_FIXTURE_H

#include <stddef.h>
#include <string.h>

#define FLOOR_FIXTURE_SIZE 256

static void
collision_put16(unsigned char *p, unsigned long n)
{
	p[0] = (unsigned char)n;
	p[1] = (unsigned char)(n >> 8);
}

static void
collision_put32(unsigned char *p, unsigned long n)
{
	collision_put16(p, n);
	collision_put16(p + 2, n >> 16);
}

/*
 * Two stacked square floors with opposing signed boundary references.
 * Optional depth-one tree exercises all XYZ child bits and shared leaves.
 */
static size_t
collision_floor_fixture(unsigned char data[FLOOR_FIXTURE_SIZE], int tree)
{
	size_t lists;
	size_t polygons;
	size_t boundaries;
	unsigned int i;
	unsigned char *p;

	memset(data, 0, FLOOR_FIXTURE_SIZE);
	collision_put16(data, 42);
	collision_put16(data + 2, (unsigned long)tree);

	for (i = 0; i < 3; i++) {
		collision_put16(data + 4 + i * 2, tree ? 2 : 1);
		collision_put32(data + 20 + i * 4, 0x40800000UL);
	}

	collision_put16(data + 10, 2);
	collision_put16(data + 12, tree ? 2 : 1);
	collision_put16(data + 14, 3);
	collision_put16(data + 16, 0xcdcd);
	collision_put16(data + 18, 4);
	collision_put32(data + 32, 0x3e000000UL);
	collision_put32(data + 36, 0x41000000UL);
	collision_put16(data + 40, tree ? 1 : 0x8001);
	memset(data + 42, 0xff, 16);

	if (tree) {
		for (i = 0; i < 8; i++)
			collision_put16(data + 58 + i * 2, i == 5 ? 0 : 0x8001);
	}

	lists = tree ? 74 : 58;
	memset(data + lists, 0xff, 4);
	collision_put16(data + lists + 4, 2);
	collision_put16(data + lists + 6, 0);
	collision_put16(data + lists + 8, 1);
	collision_put16(data + lists + 10, 0xcdcd);
	polygons = lists + 12;

	for (i = 0; i < 2; i++) {
		p = data + polygons + i * 28;
		collision_put16(p, i + 1);
		collision_put16(p + 2, 1);
		collision_put16(p + 4, 0x8002);
		collision_put16(p + 6, 3);
		collision_put16(p + 8, 0x8004);
		collision_put32(p + 16, 0x3f800000UL);

		if (i != 0)
			collision_put32(p + 24, 0xc0000000UL);
	}

	boundaries = polygons + 56;

	for (i = 0; i < 4; i++) {
		p = data + boundaries + i * 16;
		collision_put32(p + (i < 2 ? 0 : 8), 0x3f800000UL);
		collision_put32(p + 12, (i & 1) ? 0xbf800000UL : 0x3f800000UL);
	}

	return boundaries + 64;
}

#endif
