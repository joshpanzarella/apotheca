/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * The desk test of "which kind of memory": the rule (dram_kind.h) and the
 * SPL's own loop, cut out of the U-Boot patch by dram-kind-test.sh
 * (extracted.inc) and compiled here against a made-up handheld.
 *
 * The made-up handheld has memory of one kind, and one way of taking a
 * set-up of the WRONG kind:
 *   REFUSES   the set-up says it failed (what the code hopes for)
 *   NEVER     the set-up never comes back: the board resets, the mark stays
 *   LIES      the set-up says it passed, and the memory does not hold
 *   ALIASES   it says it passed, and every place past 256 MiB is the same
 *             memory as the first 256 MiB
 *   TIMESOUT  a wait in the set-up times out (mctl_gave_up); the rest of
 *             the set-up then says it passed, and, at the worst, the
 *             memory even answers the test
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dram_kind.h"

typedef uint8_t u8;
struct dram_para { unsigned int kind; };
struct dram_config { int cols, rows, bus_full_width, ranks; };
static const struct dram_para para_lpddr4 = { DRAM_KIND_LPDDR4 };
static const struct dram_para para_lpddr3 = { DRAM_KIND_LPDDR3 };

enum { REFUSES, NEVER, LIES, ALIASES, TIMESOUT, WAYS };
static const char *const kWays[] = { "refuses", "never comes back", "lies", "aliases", "times out" };
static bool mctl_gave_up;	/* dram_helpers.c's: a wait timed out */

static struct {
	unsigned int kind, way;	/* the handheld */
	unsigned int mark;	/* kept through a reset */
	unsigned int millivolts;
	unsigned int setUp;	/* the kind the memory is set up as now, 0: none */
	unsigned int flaky;	/* set-ups of the RIGHT kind that never come back */
	/* what the test reads afterwards */
	unsigned int most4, most3;	/* the highest rail a set-up of each kind began at */
	unsigned int wrongRail;		/* set-ups begun at another rail than the kind's */
	unsigned int gaveUpAtRight;	/* right-kind set-ups begun with a time-out still standing */
	unsigned int priority;
	unsigned int said;
} unit;
static jmp_buf reset;

/* The memory: only the words the test touches, so 64 of them twice over */
#define WORDS 4096
static unsigned long long placeOf[WORDS];
static unsigned int wordOf[WORDS], words;
static unsigned int *wordAt(unsigned long long place)
{
	unsigned int i;

	if (unit.setUp != unit.kind && unit.way == ALIASES)
		place &= (256ull << 20) - 1;
	for (i = 0; i < words; i++)
		if (placeOf[i] == place)
			return &wordOf[i];
	placeOf[words] = place;
	wordOf[words] = 0x12340000u + (unsigned int)(place >> 12) * 7u;	/* what was there */
	return &wordOf[words++];
}
static unsigned int dram_kind_get(unsigned long long place)
{
	if (unit.setUp != unit.kind && unit.way == LIES)
		return 0xdeadbeefu ^ (unsigned int)place;
	return *wordAt(place);
}
static void dram_kind_put(unsigned long long place, unsigned int word)
{
	if (unit.setUp != unit.kind && unit.way == LIES)
		return;
	*wordAt(place) = word;
}

static bool mctl_core_init(const struct dram_para *para, struct dram_config *config)
{
	(void)config;
	if (unit.millivolts != dram_kind_millivolts(para->kind))
		unit.wrongRail++;
	if (para->kind == DRAM_KIND_LPDDR4 && unit.millivolts > unit.most4) unit.most4 = unit.millivolts;
	if (para->kind == DRAM_KIND_LPDDR3 && unit.millivolts > unit.most3) unit.most3 = unit.millivolts;
	unit.setUp = 0;
	if (para->kind == unit.kind) {
		if (unit.flaky) {
			unit.flaky--;
			longjmp(reset, 1);
		}
		unit.setUp = para->kind;
		unit.gaveUpAtRight += mctl_gave_up;
		return true;
	}
	if (unit.way == REFUSES)
		return false;
	if (unit.way == NEVER)
		longjmp(reset, 1);
	if (unit.way == TIMESOUT) {
		mctl_gave_up = true;
		return true;
	}
	unit.setUp = para->kind;
	return true;
}
static void mctl_auto_detect_dram_size(const struct dram_para *para, struct dram_config *config)
{
	(void)para; (void)config;
}
static unsigned long mctl_calc_size(const struct dram_config *config)
{
	(void)config;
	return 1024ul << 20;
}
static void mctl_set_master_priority(void) { unit.priority++; }
static void axp_set_dcdc3(unsigned int mv) { unit.millivolts = mv; }
static void mdelay(unsigned int ms) { (void)ms; }

#define SUNXI_PRCM_BASE 0
#define CCU_PRCM_RES_CAL_CTRL 0
#define CCU_PRCM_OHMS240 0
#define BIT(n) (1u << (n))
#define setbits_le32(a, b) ((void)(a))
#define clrbits_le32(a, b) ((void)(a))
#define DRAM_KIND_MARK ((void *)0)
#define readl(a) (unit.mark)
#define writel(v, a) (unit.mark = (v))
#define printf(...) (unit.said++)

#include "extracted.inc"

#undef printf

static int failed;
#define CHECK(what, ...) do { if (!(what)) { failed++; printf("FAILED line %d: %s: ", __LINE__, #what); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Power on, and reset as often as the board does. Returns the runs it took, 0: never. */
static unsigned int start(unsigned int *found)
{
	volatile unsigned int runs;

	for (runs = 1; runs <= 8; runs++) {
		unit.millivolts = 1100;	/* the SPL's own, before the memory code */
		unit.setUp = 0;
		if (setjmp(reset))
			continue;	/* the set-up never came back */
		if (sunxi_dram_init()) {
			*found = unit.setUp;
			return runs;
		}
		/* 0: board.c panics, and the board resets */
	}
	return 0;
}

static void testTheRule(void)
{
	unsigned int m;

	CHECK(dram_kind_first(0) == DRAM_KIND_LPDDR4, "a first power-on");
	CHECK(dram_kind_first(0xffffffffu) == DRAM_KIND_LPDDR4, "all ones");
	CHECK(dram_kind_first(0x41504414u) == DRAM_KIND_LPDDR4, "GOOD lpddr4, as U-Boot reads it");
	CHECK(dram_kind_first(0x41504413u) == DRAM_KIND_LPDDR3, "GOOD lpddr3, as U-Boot reads it");
	CHECK(dram_kind_first(0x41504434u) == DRAM_KIND_LPDDR4, "PREFER lpddr4, as U-Boot writes it");
	CHECK(dram_kind_first(0x41504433u) == DRAM_KIND_LPDDR3, "PREFER lpddr3, as U-Boot writes it");
	CHECK(dram_kind_mark(DRAM_KIND_GOOD, DRAM_KIND_LPDDR4) == 0x41504414u, "the mark's number");
	CHECK(dram_kind_mark(DRAM_KIND_GOOD, DRAM_KIND_LPDDR3) == 0x41504413u, "the mark's number");
	CHECK(dram_kind_mark(DRAM_KIND_PREFER, DRAM_KIND_LPDDR4) == 0x41504434u, "the mark's number");
	CHECK(dram_kind_mark(DRAM_KIND_PREFER, DRAM_KIND_LPDDR3) == 0x41504433u, "the mark's number");
	CHECK(dram_kind_first(dram_kind_mark(DRAM_KIND_TRYING, DRAM_KIND_LPDDR4)) == DRAM_KIND_LPDDR3, "TRYING 4: the other");
	CHECK(dram_kind_first(dram_kind_mark(DRAM_KIND_TRYING, DRAM_KIND_LPDDR3)) == DRAM_KIND_LPDDR4, "TRYING 3: the other");
	CHECK(dram_kind_first(0x41504314u) == DRAM_KIND_LPDDR4, "another magic");
	CHECK(dram_kind_first(0x51504413u) == DRAM_KIND_LPDDR4, "another magic, kind 3");
	CHECK(dram_kind_first(0x41504415u) == DRAM_KIND_LPDDR4, "a kind nobody knows");
	CHECK(dram_kind_first(0x41504443u) == DRAM_KIND_LPDDR4, "a state nobody knows");
	CHECK(dram_kind_first(0x41504403u) == DRAM_KIND_LPDDR4, "no state");
	/* every mark gives one of the two */
	for (m = 0; m < 0x200; m++) {
		const unsigned int first = dram_kind_first(DRAM_KIND_MAGIC | m);
		CHECK(first == DRAM_KIND_LPDDR4 || first == DRAM_KIND_LPDDR3, "mark %x", m);
	}
	CHECK(dram_kind_other(DRAM_KIND_LPDDR4) == DRAM_KIND_LPDDR3 && dram_kind_other(DRAM_KIND_LPDDR3) == DRAM_KIND_LPDDR4, "the other");
	CHECK(dram_kind_millivolts(DRAM_KIND_LPDDR4) == 1100, "LPDDR4's rail");
	CHECK(dram_kind_millivolts(DRAM_KIND_LPDDR3) == 1200, "LPDDR3's rail");
	CHECK(dram_kind_millivolts(0) == 1100, "nothing known: the lower rail");
}

/* A memory of its own for the test of the test */
static unsigned int ram[1 << 16];	/* 256 KiB of words standing for the whole */
static unsigned long long ramSize, alias;
static unsigned int stuckAt = ~0u, stuckBit, deaf = ~0u, puts_;
static unsigned int ramIndex(unsigned long long place)
{
	if (alias)
		place %= alias;
	/* each 1/65536 of the size is one word here: the 64 places are 1024 apart */
	return (unsigned int)(place * 65536ull / ramSize);
}
static unsigned int ramGet(unsigned long long place)
{
	unsigned int w = ram[ramIndex(place)];

	if (ramIndex(place) == stuckAt)
		w |= stuckBit;
	return w;
}
static void ramPut(unsigned long long place, unsigned int word)
{
	puts_++;
	if (ramIndex(place) == deaf)
		return;
	ram[ramIndex(place)] = word;
}

static void testTheMemoryTest(void)
{
	static unsigned int before[1 << 16];
	unsigned int i, j;
	const unsigned long long sizes[] = { 512ull << 20, 1024ull << 20, 2048ull << 20, 4096ull << 20 };

	for (j = 0; j < 4; j++) {
		ramSize = sizes[j];
		alias = 0; stuckAt = ~0u; deaf = ~0u;
		for (i = 0; i < 1 << 16; i++)
			ram[i] = before[i] = i * 2654435761u;
		/* the places: inside the size, apart, on words, clear of the first words */
		for (i = 0; i < DRAM_KIND_TEST_PLACES; i++) {
			const unsigned long long p = dram_kind_test_place(ramSize, i);
			CHECK(p + 4 <= ramSize && (p & 3) == 0 && p >= 0x1000, "place %u of %llu", i, ramSize);
			if (i) CHECK(p - dram_kind_test_place(ramSize, i - 1) == ramSize / DRAM_KIND_TEST_PLACES, "the step at %u", i);
			if (i) CHECK(dram_kind_test_word(i) != dram_kind_test_word(i - 1), "the word at %u", i);
		}
		CHECK(dram_kind_test_place(ramSize, DRAM_KIND_TEST_PLACES - 1) > ramSize - ramSize / 32, "the last place is near the top");
		puts_ = 0;
		CHECK(dram_kind_test(ramSize, ramGet, ramPut) == 1, "a good memory of %llu passes", ramSize);
		CHECK(puts_ == 3 * DRAM_KIND_TEST_PLACES, "%u writes", puts_);
		CHECK(memcmp(ram, before, sizeof ram) == 0, "a good memory is left as it was");

		/* half the size is the same memory again (a wrong size, a wrong address line) */
		for (alias = ramSize / 2; alias >= ramSize / 64; alias /= 2) {
			CHECK(dram_kind_test(ramSize, ramGet, ramPut) == 0, "aliased every %llu of %llu", alias, ramSize);
		}
		alias = 0;
		/* one bit stuck high, one place that takes no write: at each place */
		for (i = 0; i < DRAM_KIND_TEST_PLACES; i++) {
			stuckAt = ramIndex(dram_kind_test_place(ramSize, i)); stuckBit = 1u << (i % 32);
			CHECK(dram_kind_test(ramSize, ramGet, ramPut) == 0, "a bit stuck at place %u", i);
			stuckAt = ~0u;
			deaf = ramIndex(dram_kind_test_place(ramSize, i));
			CHECK(dram_kind_test(ramSize, ramGet, ramPut) == 0, "place %u takes no write", i);
			deaf = ~0u;
		}
		CHECK(dram_kind_test(ramSize, ramGet, ramPut) == 1, "and good again");
	}
	CHECK(dram_kind_test(8ull << 20, ramGet, ramPut) == 0, "a size too small to be true");
	CHECK(dram_kind_test(0, ramGet, ramPut) == 0, "no size");
}

static void testTheLoop(void)
{
	const unsigned int kinds[] = { DRAM_KIND_LPDDR4, DRAM_KIND_LPDDR3 };
	const unsigned int marks[] = {
		0, 0xffffffffu, 0x12345678u,
		0x41504414u, 0x41504413u, 0x41504424u, 0x41504423u, 0x41504434u, 0x41504433u,
	};
	unsigned int k, w, m, flaky;

	for (k = 0; k < 2; k++) for (w = 0; w < WAYS; w++) for (m = 0; m < 9; m++) for (flaky = 0; flaky < 3; flaky++) {
		unsigned int found = 0, runs, again;
		char what[120];

		memset(&unit, 0, sizeof unit);
		words = 0;
		mctl_gave_up = false;
		unit.kind = kinds[k]; unit.way = w; unit.mark = marks[m]; unit.flaky = flaky;
		snprintf(what, sizeof what, "LPDDR%u, a wrong set-up %s, mark %08x, %u flaky", kinds[k], kWays[w], marks[m], flaky);

		runs = start(&found);
		CHECK(runs != 0, "%s: never started", what);
		CHECK(found == kinds[k], "%s: took LPDDR%u", what, found);
		CHECK(unit.mark == dram_kind_mark(DRAM_KIND_GOOD, kinds[k]), "%s: the mark is %08x", what, unit.mark);
		CHECK(unit.millivolts == dram_kind_millivolts(kinds[k]), "%s: the rail is left at %u", what, unit.millivolts);
		CHECK(unit.gaveUpAtRight == 0, "%s: a time-out of the try before was left standing", what);
		CHECK(unit.wrongRail == 0, "%s: %u set-ups at another kind's rail", what, unit.wrongRail);
		CHECK(unit.most4 <= 1100, "%s: LPDDR4 settings at %u mV", what, unit.most4);
		CHECK(unit.priority == 1 && unit.said == 1, "%s: priority %u, said %u", what, unit.priority, unit.said);
		/* how many runs it may take: one, and one more for each set-up that
		 * never came back (where the wrong kind never comes back either,
		 * they go turn about: two a flaky one, and one to begin with) */
		CHECK(runs <= (w == NEVER ? 2 + 2 * flaky : 1 + flaky), "%s: %u runs", what, runs);
		if (flaky == 0 && marks[m] == dram_kind_mark(DRAM_KIND_GOOD, kinds[k]))
			CHECK(runs == 1, "%s: %u runs", what, runs);
		/* with nothing known and nothing flaky, an LPDDR4 handheld never sees 1.2 V */
		if (kinds[k] == DRAM_KIND_LPDDR4 && flaky == 0 && m < 4)
			CHECK(unit.most3 == 0, "%s: an LPDDR3 set-up was tried", what);
		/* the memory is as it was before the test */
		{
			unsigned int i;
			for (i = 0; i < words; i++)
				CHECK(wordOf[i] == 0x12340000u + (unsigned int)(placeOf[i] >> 12) * 7u, "%s: place %llx changed", what, placeOf[i]);
		}

		/* the next start: one run, the right kind first, the other never tried */
		unit.most3 = unit.most4 = 0; unit.priority = unit.said = 0;
		again = start(&found);
		CHECK(again == 1 && found == kinds[k], "%s: the next start took %u runs, LPDDR%u", what, again, found);
		CHECK((kinds[k] == DRAM_KIND_LPDDR4 ? unit.most3 : unit.most4) == 0, "%s: the next start tried the other kind", what);
	}

	/* No kind works (a broken handheld): PREFER the other, and it goes on turn about */
	memset(&unit, 0, sizeof unit);
	unit.kind = 9; unit.way = REFUSES;
	CHECK(sunxi_dram_init() == 0, "no kind works: 0");
	CHECK(unit.mark == dram_kind_mark(DRAM_KIND_PREFER, DRAM_KIND_LPDDR3), "then LPDDR3 first: %08x", unit.mark);
	CHECK(unit.priority == 0 && unit.said == 0, "nothing said");
	CHECK(sunxi_dram_init() == 0, "again 0");
	CHECK(unit.mark == dram_kind_mark(DRAM_KIND_PREFER, DRAM_KIND_LPDDR4), "then LPDDR4 first: %08x", unit.mark);
}

int main(void)
{
	testTheRule();
	testTheMemoryTest();
	testTheLoop();
	if (failed) {
		printf("dram-kind-test: %d FAILED\n", failed);
		return 1;
	}
	printf("dram-kind-test: all passed\n");
	return 0;
}
