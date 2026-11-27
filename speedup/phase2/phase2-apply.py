#!/usr/bin/env python3
"""Apply Phase 2 walk-summary optimization to robang74/nist-sts-ng:uchaosys.

Run from the repository root:
    python3 /path/to/phase2-apply.py --check
    python3 /path/to/phase2-apply.py

The script edits files only after all expected source markers have matched.
"""

from __future__ import print_function
import argparse
import pathlib
import sys

ROOT = pathlib.Path.cwd()


def fail(message):
    raise SystemExit("phase2-apply: " + message)


def read(rel):
    path = ROOT / rel
    if not path.is_file():
        fail("missing source file: " + rel)
    return path.read_text(encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail("expected exactly one match for %s, found %d" % (label, count))
    return text.replace(old, new, 1)


def replace_between(text, start, end, replacement, label):
    first = text.find(start)
    if first < 0:
        fail("start marker not found for " + label)
    second = text.find(end, first + len(start))
    if second < 0:
        fail("end marker not found for " + label)
    if text.find(start, first + len(start)) >= 0 and label.endswith("(unique)"):
        fail("start marker is not unique for " + label)
    return text[:first] + replacement + text[second:]


def transform_defs(text):
    if "excursion_cycle_bins" in text:
        fail("defs.h already contains Phase 2 excursion summaries")
    old = """struct sts_walk_summary {
\tlong int sum;             /* final +/-1 walk sum */
\tlong int sup;             /* maximum partial sum, including initial zero */
\tlong int inf;             /* minimum partial sum, including initial zero */
\tlong int invalid;         /* input bytes other than 0 or 1 */
};"""
    new = """struct sts_walk_summary {
\tlong int sum;             /* final +/-1 walk sum */
\tlong int sup;             /* maximum partial sum, including initial zero */
\tlong int inf;             /* minimum partial sum, including initial zero */
\tlong int invalid;         /* input bytes other than 0 or 1 */
\tlong int excursion_cycles; /* completed and final partial cycles */
\tlong int excursion_cycle_bins[DEGREES_OF_FREEDOM_RND_EXCURSION]
\t\t[NUMBER_OF_STATES_RND_EXCURSION];
\tlong int excursion_last_cycle_visits[NUMBER_OF_STATES_RND_EXCURSION];
\tlong int excursion_variant_visits[NUMBER_OF_STATES_RND_EXCURSION_VAR];
};"""
    return replace_once(text, old, new, "sts_walk_summary definition")


def transform_packed(text):
    if "sts_finish_excursion_cycle" in text:
        fail("packed.c already contains Phase 2 excursion summaries")

    # The current upstream source has a closing parenthesis in the SWAR line;
    # use a smaller stable insertion anchor instead of replacing that function.
    anchor = "\nvoid\nsts_prepare_packed_stream(struct state *state, long int thread_id)\n{"
    helper = """
static void
sts_finish_excursion_cycle(struct sts_walk_summary *walk, long int *counts)
{
\tunsigned int i;
\tunsigned int bin;

\tfor (i = 0; i < NUMBER_OF_STATES_RND_EXCURSION; ++i) {
\t\tbin = (counts[i] >= DEGREES_OF_FREEDOM_RND_EXCURSION - 1)
\t\t\t? DEGREES_OF_FREEDOM_RND_EXCURSION - 1 : (unsigned int) counts[i];
\t\twalk->excursion_cycle_bins[bin][i]++;
\t\twalk->excursion_last_cycle_visits[i] = counts[i];
\t\tcounts[i] = 0;
\t}
\twalk->excursion_cycles++;
}
"""
    text = replace_once(text, anchor, "\n" + helper + anchor[1:], "packed cycle helper insertion")

    text = replace_once(
        text,
        "\tlong int sum = 0;\n\tstruct sts_walk_summary *walk;",
        "\tlong int sum = 0;\n\tlong int excursion_counts[NUMBER_OF_STATES_RND_EXCURSION] = { 0 };\n\tstruct sts_walk_summary *walk;",
        "packed local excursion counters",
    )

    old_loop_tail = """\t\tif (sum > walk->sup)
\t\t\twalk->sup = sum;
\t\tif (sum < walk->inf)
\t\t\twalk->inf = sum;
\t}
\twalk->sum = sum;"""
    new_loop_tail = """\t\tif (sum > walk->sup)
\t\t\twalk->sup = sum;
\t\tif (sum < walk->inf)
\t\t\twalk->inf = sum;

\t\t/* Accumulate state visits in the current cycle in the same pass. */
\t\tif (sum == 0) {
\t\t\tsts_finish_excursion_cycle(walk, excursion_counts);
\t\t} else {
\t\t\tlong int magnitude = (sum < 0) ? -sum : sum;
\t\t\tif (magnitude <= MAX_EXCURSION_RND_EXCURSION) {
\t\t\t\tlong int index = sum + ((sum < 0)
\t\t\t\t\t? MAX_EXCURSION_RND_EXCURSION : MAX_EXCURSION_RND_EXCURSION - 1);
\t\t\t\texcursion_counts[index]++;
\t\t\t}
\t\t\tif (magnitude <= MAX_EXCURSION_RND_EXCURSION_VAR) {
\t\t\t\tlong int index = sum + ((sum < 0)
\t\t\t\t\t? MAX_EXCURSION_RND_EXCURSION_VAR : MAX_EXCURSION_RND_EXCURSION_VAR - 1);
\t\t\t\twalk->excursion_variant_visits[index]++;
\t\t\t}
\t\t}
\t}
\t/* An unfinished tail is the final (non-returning) cycle. */
\tif (sum != 0)
\t\tsts_finish_excursion_cycle(walk, excursion_counts);
\twalk->sum = sum;"""
    return replace_once(text, old_loop_tail, new_loop_tail, "packed streaming excursion accumulation")


def transform_driver(text):
    old = """\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||
\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM]) {"""
    new = """\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||
\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM] ||
\t    state->testVector[TEST_RND_EXCURSION] || state->testVector[TEST_RND_EXCURSION_VAR]) {"""
    return replace_once(text, old, new, "driver packed-buffer selection")


def transform_random_excursions_init(text):
    start = "\t/*\n\t * Allocate the partial sums and the cycle arrays for each thread\n\t */"
    end = "\t/*\n\t * Create working sub-directory if forming files such as results.txt and stats.txt\n\t */"
    return replace_between(text, start, end, "", "RandomExcursions_init obsolete O(n) buffers")


def transform_random_excursions_iterate(text):
    if "excursion_last_cycle_visits" in text:
        fail("randomExcursions.c already contains Phase 2 summary consumer")

    fstart = text.find("RandomExcursions_iterate(struct thread_state *thread_state)")
    fend = text.find("/*\n * RandomExcursions_print_stat", fstart)
    if fstart < 0 or fend < 0:
        fail("cannot isolate RandomExcursions_iterate")
    pre, body, post = text[:fstart], text[fstart:fend], text[fend:]

    decl_start = body.find("\tstruct RandomExcursions_private_stats stat;")
    decl_end = body.find("\t/*\n\t * Check preconditions (firewall)\n\t */", decl_start)
    if decl_start < 0 or decl_end < 0:
        fail("cannot locate RandomExcursions_iterate declarations")
    declarations = """\tstruct RandomExcursions_private_stats stat;\t// Stats for this iteration
\tstruct sts_walk_summary *walk;
\tlong int v[DEGREES_OF_FREEDOM_RND_EXCURSION][NUMBER_OF_STATES_RND_EXCURSION];\t// Global frequency counters
\tlong int x;\t\t\t// State value to test
\tlong int labs_x;\t\t\t// Absolute value of the state value x
\tdouble p_value;\t\t\t// p_value iteration test result(s)
\tdouble *p_values;\t\t// Array of p-values produced by this test
\tdouble sum_term;\t\t// Value whose square is used to compute the test statistic
\tlong int i;
\tlong int j;
"""
    body = body[:decl_start] + declarations + body[decl_end:]

    start = "\tif (state->rnd_excursion_S == NULL) {"
    end = "\tif (state->cSetup != true) {"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursions_iterate legacy buffer checks")
    checks = """\tif (state->walk_summary == NULL) {
\t\terr(151, __func__, "state->walk_summary is NULL");
\t}
\twalk = &state->walk_summary[thread_state->thread_id];
\tif (walk->invalid != 0) {
\t\terr(41, __func__, "found a bit different than 1 or 0 in the sequence");
\t}
"""
    body = body[:p1] + checks + body[p2:]

    start = "\t/*\n\t * Collect parameters from state\n\t */"
    end = "\tif (stat.test_possible == true) {"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursions_iterate legacy summary computation")
    summary = """\tstat.number_of_cycles = walk->excursion_cycles;
\tmemcpy(v, walk->excursion_cycle_bins, sizeof(v));
\tmemcpy(stat.counter, walk->excursion_last_cycle_visits, sizeof(stat.counter));
\tstat.test_possible = (stat.number_of_cycles < state->c.min_zero_crossings) ? false : true;

"""
    body = body[:p1] + summary + body[p2:]

    start = "\t\t/*\n\t\t * Zeroize variables and global counters\n\t\t */"
    end = "\t\tp_values = malloc(NUMBER_OF_STATES_RND_EXCURSION * sizeof(*p_values));"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursions_iterate redundant cycle scans")
    body = body[:p1] + body[p2:]
    return pre + body + post


def transform_random_excursions_destroy(text):
    fstart = text.find("RandomExcursions_destroy(struct state *state)")
    if fstart < 0:
        fail("cannot isolate RandomExcursions_destroy")
    pre, body = text[:fstart], text[fstart:]
    start = "\tfor (i = 0; i < state->numberOfThreads; i++) {\n\t\tif (state->rnd_excursion_S[i] != NULL) {"
    end = "\t// Free the theoretical probabilities matrix"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursions_destroy legacy buffers")
    replacement = """\tif (state->rnd_excursion_S != NULL) {
\t\tfor (i = 0; i < state->numberOfThreads; i++) {
\t\t\tif (state->rnd_excursion_S[i] != NULL)
\t\t\t\tfree(state->rnd_excursion_S[i]);
\t\t}
\t\tfree(state->rnd_excursion_S);
\t\tstate->rnd_excursion_S = NULL;
\t}
\tif (state->rnd_excursion_cycle != NULL) {
\t\tfor (i = 0; i < state->numberOfThreads; i++) {
\t\t\tif (state->rnd_excursion_cycle[i] != NULL)
\t\t\t\tfree_dyn_array(state->rnd_excursion_cycle[i]);
\t\t}
\t\tfree(state->rnd_excursion_cycle);
\t\tstate->rnd_excursion_cycle = NULL;
\t}
"""
    return pre + body[:p1] + replacement + body[p2:]


def transform_variant_init(text):
    start = "\t/*\n\t * Allocate partial sums working array for each thread\n\t */"
    end = "\t/*\n\t * Allocate dynamic arrays\n\t */"
    return replace_between(text, start, end, "", "RandomExcursionsVariant_init obsolete O(n) buffer")


def transform_variant_iterate(text):
    if "excursion_variant_visits" in text:
        fail("randomExcursionsVariant.c already contains Phase 2 summary consumer")

    fstart = text.find("RandomExcursionsVariant_iterate(struct thread_state *thread_state)")
    fend = text.find("/*\n * RandomExcursionsVariant_print_stat", fstart)
    if fstart < 0 or fend < 0:
        fail("cannot isolate RandomExcursionsVariant_iterate")
    pre, body, post = text[:fstart], text[fstart:fend], text[fend:]

    decl_start = body.find("\tstruct RandomExcursionsVariant_private_stats stat;")
    decl_end = body.find("\t/*\n\t * Check preconditions (firewall)\n\t */", decl_start)
    if decl_start < 0 or decl_end < 0:
        fail("cannot locate RandomExcursionsVariant_iterate declarations")
    declarations = """\tstruct RandomExcursionsVariant_private_stats stat;\t// Stats for this iteration
\tstruct sts_walk_summary *walk;
\tdouble p_value;\t\t// p_value iteration test result(s)
\tdouble *p_values;\t// Array of p-values produced by this test
\tlong int i;
"""
    body = body[:decl_start] + declarations + body[decl_end:]

    start = "\tif (state->ex_var_partial_sums == NULL) {"
    end = "\tif (state->cSetup != true) {"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursionsVariant_iterate legacy buffer checks")
    checks = """\tif (state->walk_summary == NULL) {
\t\terr(161, __func__, "state->walk_summary is NULL");
\t}
\twalk = &state->walk_summary[thread_state->thread_id];
\tif (walk->invalid != 0) {
\t\terr(41, __func__, "found a bit different than 1 or 0 in the sequence");
\t}
"""
    body = body[:p1] + checks + body[p2:]

    start = "\t/*\n\t * Collect parameters from state\n\t */"
    end = "\tif (stat.test_possible == true) {"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursionsVariant_iterate legacy summary computation")
    summary = """\tstat.number_of_cycles = walk->excursion_cycles;
\tmemcpy(stat.counter, walk->excursion_variant_visits, sizeof(stat.counter));
\tstat.test_possible = (stat.number_of_cycles < state->c.min_zero_crossings) ? false : true;

"""
    body = body[:p1] + summary + body[p2:]

    count_start = "\t\t\t/*\n\t\t\t * Step 4: count times when the partial sum matches this excursion state value\n\t\t\t */"
    count_end = "\t\t\t/*\n\t\t\t * Step 5: compute the test p-value for this excursion state value\n\t\t\t */"
    p1 = body.find(count_start)
    p2 = body.find(count_end, p1 + len(count_start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursionsVariant per-state O(n) scan")
    body = body[:p1] + body[p2:]
    return pre + body + post


def transform_variant_destroy(text):
    fstart = text.find("RandomExcursionsVariant_destroy(struct state *state)")
    if fstart < 0:
        fail("cannot isolate RandomExcursionsVariant_destroy")
    pre, body = text[:fstart], text[fstart:]
    start = "\tfor (i = 0; i < state->numberOfThreads; i++) {\n\t\tif (state->ex_var_partial_sums[i] != NULL) {"
    end = "\treturn;\n}"
    p1 = body.find(start)
    p2 = body.find(end, p1 + len(start))
    if p1 < 0 or p2 < 0:
        fail("cannot locate RandomExcursionsVariant_destroy legacy buffers")
    replacement = """\tif (state->ex_var_partial_sums != NULL) {
\t\tfor (i = 0; i < state->numberOfThreads; i++) {
\t\t\tif (state->ex_var_partial_sums[i] != NULL)
\t\t\t\tfree(state->ex_var_partial_sums[i]);
\t\t}
\t\tfree(state->ex_var_partial_sums);
\t\tstate->ex_var_partial_sums = NULL;
\t}
\treturn;
}"""
    return pre + body[:p1] + replacement + body[p2 + len(end):]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate source markers without writing files")
    args = parser.parse_args()

    paths = {
        "src/utils/defs.h": transform_defs,
        "src/utils/packed.c": transform_packed,
        "src/utils/driver.c": transform_driver,
        "src/tests/randomExcursions.c": transform_random_excursions_init,
        "src/tests/randomExcursionsVariant.c": transform_variant_init,
    }
    original = {rel: read(rel) for rel in paths}
    updated = {rel: fn(original[rel]) for rel, fn in paths.items()}

    # The test files each contain multiple independently anchored changes.
    updated["src/tests/randomExcursions.c"] = transform_random_excursions_iterate(updated["src/tests/randomExcursions.c"])
    updated["src/tests/randomExcursions.c"] = transform_random_excursions_destroy(updated["src/tests/randomExcursions.c"])
    updated["src/tests/randomExcursionsVariant.c"] = transform_variant_iterate(updated["src/tests/randomExcursionsVariant.c"])
    updated["src/tests/randomExcursionsVariant.c"] = transform_variant_destroy(updated["src/tests/randomExcursionsVariant.c"])

    changed = [rel for rel in paths if original[rel] != updated[rel]]
    if not changed:
        fail("no changes produced")
    if args.check:
        print("Phase 2 source markers OK; would update:")
        for rel in changed:
            print("  " + rel)
        return

    for rel in changed:
        (ROOT / rel).write_text(updated[rel], encoding="utf-8")
    print("Updated:")
    for rel in changed:
        print("  " + rel)
    print("Review with: git diff --check && git diff")


if __name__ == "__main__":
    main()
