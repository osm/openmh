/* Synthetic convex box: X/Y +/-1, Z +/-2. No shipped bytes. */
static void
pair_put16(unsigned char *p, unsigned long n)
{
	p[0] = (unsigned char)n;
	p[1] = (unsigned char)(n >> 8);
}

static void
pair_put32(unsigned char *p, unsigned long n)
{
	pair_put16(p, n);
	pair_put16(p + 2, n >> 16);
}

static size_t
pair_fixture(unsigned char data[278])
{
	unsigned int i;
	unsigned char *p;

	memset(data, 0, 278);
	pair_put16(data, 42);

	for (i = 0; i < 3; i++)
		pair_put16(data + 4 + 2 * i, 1);

	pair_put16(data + 10, 6);
	pair_put16(data + 12, 1);
	pair_put16(data + 14, 1);
	pair_put16(data + 18, 3);
	pair_put32(data + 32, 0x3f800000UL);
	pair_put32(data + 36, 0x3f800000UL);

	for (i = 0; i < 6; i++) {
		p = data + 62 + 28 * i;
		pair_put16(p + 2, 1);
		pair_put16(p + 4, 2);
		pair_put16(p + 6, 3);
		pair_put32(p + 12 + 4 * (i / 2),
			(i & 1) ? 0xbf800000UL : 0x3f800000UL);
		pair_put32(p + 24, i < 4 ? 0xbf800000UL : 0xc0000000UL);
	}

	for (i = 0; i < 3; i++)
		pair_put32(data + 230 + 16 * i + 4 * i, 0x3f800000UL);

	return 278;
}
