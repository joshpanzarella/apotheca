#!/bin/sh
# The desk test of "which kind of memory" (LPDDR4 or LPDDR3) in the first
# program (U-Boot's SPL):
#   - the rule in os/board/rg40xxv/uboot/dram_kind.h,
#   - that the U-Boot patch carries that file to the byte,
#   - the SPL's own loop, cut out of the patch and run against a made-up
#     handheld of each kind (os/tools/dram-kind/test.c says how it is made up).
# No hardware is in it: whether a real memory answers as the made-up one does
# is for the handhelds.
#
#   os/tools/dram-kind-test.sh            run it
#   os/tools/dram-kind-test.sh --mutants  break the rule and the loop on
#                                         purpose: each must fail
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
rule=$top/os/board/rg40xxv/uboot/dram_kind.h
patch=$top/os/board/rg40xxv/patches/uboot/0004-apotheca-sunxi-h616-lpddr4-or-lpddr3.patch
work=$(mktemp -d "${TMPDIR:-/tmp}/apotheca-dram-kind-XXXXXX")
trap 'rm -rf "$work"' EXIT

# The patch's copy of the rule
awk '/^\+\+\+ b\/arch\/arm\/include\/asm\/arch-sunxi\/dram_kind.h/ { on = 1; next }
     on && /^@@/ { next }
     on && /^(diff|--- |-- $)/ { on = 0 }
     on { sub(/^\+/, ""); print }' "$patch" > "$work/patch-rule.h"
if ! cmp -s "$rule" "$work/patch-rule.h"; then
    echo "FAILED: the patch's dram_kind.h is not os/board/rg40xxv/uboot/dram_kind.h"
    diff "$rule" "$work/patch-rule.h" | head -n 20
    exit 1
fi

# The loop: from dram_kind_rank_width() to the end of sunxi_dram_init()
awk '/^\+static bool dram_kind_rank_width/ { on = 1 }
     on { sub(/^\+/, ""); print }
     on && /^\+?};?$/ && seen { exit }
     on && /^unsigned long sunxi_dram_init/ { seen = 1 }' "$patch" > "$work/extracted.inc"
grep -q 'dram_kind_try(kind)' "$work/extracted.inc" && grep -q 'axp_set_dcdc3' "$work/extracted.inc" \
    || { echo "FAILED: the loop was not found in the patch"; exit 1; }

build() { # rule, loop, out
    mkdir -p "$work/b"
    cp "$1" "$work/b/dram_kind.h"; cp "$2" "$work/b/extracted.inc"; cp "$here/dram-kind/test.c" "$work/b/test.c"
    ${CC:-cc} -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-function -o "$3" "$work/b/test.c"
}

build "$rule" "$work/extracted.inc" "$work/test"
"$work/test"

[ "${1:-}" = "--mutants" ] || exit 0

died=0 lived=0
mutant() { # name, file (rule|loop), sed expression
    cp "$rule" "$work/m-rule.h"; cp "$work/extracted.inc" "$work/m-loop.inc"
    case "$2" in rule) f=$work/m-rule.h ;; *) f=$work/m-loop.inc ;; esac
    sed -i "$3" "$f"
    if cmp -s "$f" "$rule" || cmp -s "$f" "$work/extracted.inc"; then
        echo "MUTANT NOT MADE: $1"; lived=$((lived + 1)); return
    fi
    if build "$work/m-rule.h" "$work/m-loop.inc" "$work/m-test" 2>/dev/null && "$work/m-test" > /dev/null 2>&1; then
        echo "MUTANT LIVES: $1"; lived=$((lived + 1))
    else
        died=$((died + 1))
    fi
}
mutant "TRYING gives the same kind again" rule 's/return dram_kind_other(kind);/return kind;/'
mutant "GOOD is not believed" rule 's/state == DRAM_KIND_GOOD || state == DRAM_KIND_PREFER/state == DRAM_KIND_PREFER/'
mutant "PREFER is not believed" rule 's/state == DRAM_KIND_GOOD || state == DRAM_KIND_PREFER/state == DRAM_KIND_GOOD/'
mutant "the magic is not looked at" rule 's/if ((mark & DRAM_KIND_MAGIC_MASK) != DRAM_KIND_MAGIC)/if (0)/'
mutant "LPDDR3 first when nothing is known" rule '/!= DRAM_KIND_MAGIC)/{n;s/DRAM_KIND_LPDDR4/DRAM_KIND_LPDDR3/}'
mutant "LPDDR4 at 1.2 V" rule 's/? 1200 : 1100/? 1200 : 1200/'
mutant "LPDDR3 at 1.1 V" rule 's/? 1200 : 1100/? 1100 : 1100/'
mutant "the memory test reads nothing back" rule 's/!= dram_kind_test_word(i))/!= dram_kind_test_word(i) \&\& 0)/'
mutant "no second pattern" rule 's/!= ~dram_kind_test_word(i))/!= ~dram_kind_test_word(i) \&\& 0)/'
mutant "every place the same word" rule 's/return 0xa5c30f00u ^ (i \* 0x01010101u) ^ (i << 13);/return 0xa5c30f00u;/'
mutant "the memory is not put back" rule 's/put(dram_kind_test_place(size, i), was\[i\]);/(void)was[i];/'
mutant "only the low half is tested" rule 's/return ((size \/ DRAM_KIND_TEST_PLACES) \* i + 0x1040)/return ((size \/ DRAM_KIND_TEST_PLACES \/ 2) * i + 0x1040)/'
mutant "a small size passes" rule 's/if (size < (16ull << 20))/if (0)/'
mutant "no memory test after a set-up" loop 's/if (!dram_kind_test(size, dram_kind_get, dram_kind_put))/if (0)/'
mutant "the rail is not set" loop 's/axp_set_dcdc3(dram_kind_millivolts(kind));/(void)kind;/'
mutant "no TRYING mark before a try" loop 's/writel(dram_kind_mark(DRAM_KIND_TRYING, kind), DRAM_KIND_MARK);/(void)0;/'
mutant "no GOOD mark" loop 's/writel(dram_kind_mark(DRAM_KIND_GOOD, kind), DRAM_KIND_MARK);/(void)0;/'
mutant "only one kind is tried" loop 's/for (n = 0; n < 2; n++)/for (n = 0; n < 1; n++)/'
mutant "the same kind twice" loop 's/kind = n ? dram_kind_other(first) : first;/kind = first;/'
# (either check alone is the other's belt: a set-up refused in the search is
# refused again at the end)
mutant "a refused set-up is taken" loop 's/if (!dram_kind_rank_width(para, &config))/(void)dram_kind_rank_width(para, \&config); if (0)/; s/if (!mctl_core_init(para, &config) || mctl_gave_up)/if (mctl_gave_up)/'
mutant "a set-up that timed out is taken" loop 's/return !mctl_gave_up;/return true;/; s/if (!mctl_core_init(para, &config) || mctl_gave_up)/if (!mctl_core_init(para, \&config))/'
mutant "a time-out is never cleared" loop 's/^\tmctl_gave_up = false;$/\t(void)0;/'
mutant "nothing kept when both fail" loop 's/writel(dram_kind_mark(DRAM_KIND_PREFER, dram_kind_other(first)), DRAM_KIND_MARK);/(void)first;/'
mutant "no priorities set" loop 's/mctl_set_master_priority();/(void)0;/'
echo "mutants: $died died, $lived lived"
[ "$lived" = 0 ]
