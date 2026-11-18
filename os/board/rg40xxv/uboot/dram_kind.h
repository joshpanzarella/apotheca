/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Which kind of memory to try, for a board made with LPDDR4 or LPDDR3
 * (CONFIG_SUNXI_DRAM_H616_LPDDR4_LPDDR3): the rule alone, no hardware in it,
 * so it is tested on the desk (os/tools/dram-kind-test.sh in Apotheca's
 * tree, where this file's one true copy lives; the U-Boot patch carries a
 * copy the test holds to it).
 *
 * The SPL keeps one word, the MARK, in a register that a reset does not
 * clear (the RTC's general purpose register 7 on the H616):
 *
 *   GOOD kind     the last set-up of this kind passed, and so did the test
 *                 of the memory after it: try this kind first
 *   TRYING kind   a set-up of this kind is under way. Found at the START of
 *                 a run it means that set-up never came back (the memory
 *                 code panicked or timed out and the board reset): try the
 *                 OTHER kind first
 *   PREFER kind   both kinds failed in one run, or somebody asked for this
 *                 kind (U-Boot, for a file on the card): try it first
 *   anything else (a first power-on, a lost mark): LPDDR4 first
 *
 * LPDDR4 is first by default because its rail is the lower one (1.1 V): an
 * LPDDR3 part is never harmed by 1.1 V, and an LPDDR4 part sees 1.2 V only
 * after its own kind has failed.
 */
#ifndef _DRAM_KIND_H
#define _DRAM_KIND_H

#define DRAM_KIND_MAGIC		0x41504400u	/* "APD", then the state and kind */
#define DRAM_KIND_MAGIC_MASK	0xffffff00u
#define DRAM_KIND_GOOD		0x10u
#define DRAM_KIND_TRYING	0x20u
#define DRAM_KIND_PREFER	0x30u
#define DRAM_KIND_LPDDR4	4u
#define DRAM_KIND_LPDDR3	3u

static inline unsigned int dram_kind_other(unsigned int kind)
{
	return kind == DRAM_KIND_LPDDR3 ? DRAM_KIND_LPDDR4 : DRAM_KIND_LPDDR3;
}

static inline unsigned int dram_kind_mark(unsigned int state, unsigned int kind)
{
	return DRAM_KIND_MAGIC | state | kind;
}

/* The kind to try first, from the mark as found when the SPL starts. */
static inline unsigned int dram_kind_first(unsigned int mark)
{
	unsigned int state = mark & 0xf0u, kind = mark & 0x0fu;

	if ((mark & DRAM_KIND_MAGIC_MASK) != DRAM_KIND_MAGIC)
		return DRAM_KIND_LPDDR4;
	if (kind != DRAM_KIND_LPDDR4 && kind != DRAM_KIND_LPDDR3)
		return DRAM_KIND_LPDDR4;
	if (state == DRAM_KIND_GOOD || state == DRAM_KIND_PREFER)
		return kind;
	if (state == DRAM_KIND_TRYING)
		return dram_kind_other(kind);
	return DRAM_KIND_LPDDR4;
}

/* The memory's rail for a kind, in millivolts. */
static inline unsigned int dram_kind_millivolts(unsigned int kind)
{
	return kind == DRAM_KIND_LPDDR3 ? 1200 : 1100;
}

/*
 * The test of the memory after a set-up that said it passed: COUNT places
 * spread over the size, each given its own word, all read back, then put
 * back as they were (what the kernel of the start before said is still in
 * memory after a reset, and must stay). The three steps are given the
 * memory through two small functions so the desk can run them on a made-up
 * memory.
 */
#define DRAM_KIND_TEST_PLACES	64

static inline unsigned long long dram_kind_test_place(unsigned long long size, unsigned int i)
{
	/* never the first words (the size detection writes there), 4-byte steps */
	return ((size / DRAM_KIND_TEST_PLACES) * i + 0x1040) & ~3ull;
}

static inline unsigned int dram_kind_test_word(unsigned int i)
{
	return 0xa5c30f00u ^ (i * 0x01010101u) ^ (i << 13);
}

static inline int dram_kind_test(unsigned long long size,
				 unsigned int (*get)(unsigned long long place),
				 void (*put)(unsigned long long place, unsigned int word))
{
	unsigned int was[DRAM_KIND_TEST_PLACES], i;
	int good = 1;

	if (size < (16ull << 20))
		return 0;
	for (i = 0; i < DRAM_KIND_TEST_PLACES; i++)
		was[i] = get(dram_kind_test_place(size, i));
	for (i = 0; i < DRAM_KIND_TEST_PLACES; i++)
		put(dram_kind_test_place(size, i), dram_kind_test_word(i));
	for (i = 0; i < DRAM_KIND_TEST_PLACES; i++)
		if (get(dram_kind_test_place(size, i)) != dram_kind_test_word(i))
			good = 0;
	/* and the other way up, so a place stuck at a word is caught */
	for (i = 0; i < DRAM_KIND_TEST_PLACES; i++)
		put(dram_kind_test_place(size, i), ~dram_kind_test_word(i));
	for (i = 0; i < DRAM_KIND_TEST_PLACES; i++)
		if (get(dram_kind_test_place(size, i)) != ~dram_kind_test_word(i))
			good = 0;
	for (i = 0; i < DRAM_KIND_TEST_PLACES; i++)
		put(dram_kind_test_place(size, i), was[i]);
	return good;
}

#endif
