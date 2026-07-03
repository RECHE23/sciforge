"""Mode 3 of the corpus substrate: a deterministic exhaustive enumerator over a small pattern/input
space, for differential testing (real vs an oracle) where the historical bug class lives — alternation
selection (``a|ab``, ``aabaa|b``) and empty-match edges.

One substrate, three modes: a curated static corpus (the runner over vendored files), a random sweep
(the differential fuzzer), and this — every pattern up to *k* constructs over a tiny alphabet crossed
with every input up to length *n*. It is a generator, not a data file, so it presumes nothing about
provenance; it feeds the same :func:`sciforge.corpus.classify` rules and the same report shape.

The construct set is **tiered**. Tier 1 (here) is literals, ``|``, ``*``, ``+``, ``?``, groups,
``[ab]`` / ``[^ab]``, ``.``, ``^``, ``$`` — the surface the historical bugs lived on. Later tiers add
``{n,m}`` / shorthand classes / ``\\b`` once tier 1 is measured and gated.
"""

import itertools

#: Tier-1 non-recursive atoms over an alphabet (each costs one construct).
def _tier1_atoms(alphabet):
    return list(alphabet) + [".", "[" + alphabet + "]", "[^" + alphabet + "]"]

_QUANTS = ("*", "+", "?")
_ANCHORS = ("^", "$")
_MAX_BRANCHES = 3  #: cap on alternation branches (keeps the space finite and PR-sized).
_MAX_SEQ = 4       #: cap on concatenated elements per sequence.


def _atoms(budget, alphabet, empty_branches=False):
    """Yield (string, size) for atoms within `budget`: the leaf atoms and parenthesised groups."""
    if budget < 1:
        return
    for atom in _tier1_atoms(alphabet):
        yield atom, 1
    # a group ( alternation ) costs one construct for the parens plus its inner size
    if budget >= 2:
        for inner, size in _alts(budget - 1, alphabet, empty_branches=empty_branches):
            yield "(" + inner + ")", size + 1


def _elements(budget, alphabet, empty_branches=False):
    """Yield (string, size) for one sequence element: an anchor, an atom, or a quantified atom."""
    if budget < 1:
        return
    for anchor in _ANCHORS:
        yield anchor, 1
    for atom, size in _atoms(budget, alphabet, empty_branches):
        yield atom, size
        if size + 1 <= budget:
            for quant in _QUANTS:
                yield atom + quant, size + 1


def _seqs(budget, alphabet, depth=_MAX_SEQ, empty_branches=False):
    """Yield (string, size) for sequences (>= 1 concatenated elements) within `budget`."""
    for element, size in _elements(budget, alphabet, empty_branches):
        yield element, size
        if depth > 1:
            for rest, rest_size in _seqs(budget - size, alphabet, depth - 1, empty_branches):
                yield element + rest, size + rest_size


def _alts(budget, alphabet, branches=_MAX_BRANCHES, empty_branches=False):
    """Yield (string, size) for alternations (1..branches sequences joined by ``|``) within `budget`.

    With ``empty_branches`` (tier 2) a branch may be the empty string, so ``|a`` / ``a|`` / ``|a|b`` are
    generated — the nullable-alternation class the tier-1 grammar could not reach (an empty branch under
    a quantifier is exactly where the greedy-loop empty-preference bugs live).
    """
    for sequence, size in _seqs(budget, alphabet, empty_branches=empty_branches):
        yield sequence, size
        if branches > 1 and size + 1 <= budget:
            for rest, rest_size in _alts(budget - size - 1, alphabet, branches - 1, empty_branches):
                yield sequence + "|" + rest, size + 1 + rest_size
            if empty_branches:
                yield sequence + "|", size + 1   # empty LAST branch: `a|`
    if empty_branches and branches > 1 and budget >= 1:
        for rest, rest_size in _alts(budget - 1, alphabet, branches - 1, empty_branches):
            yield "|" + rest, 1 + rest_size      # empty FIRST branch: `|a`


def enumerate_patterns(k, alphabet="ab", tier=1):
    """Return the sorted, de-duplicated list of all patterns of at most `k` constructs.

    Sorted so two runs are byte-identical (determinism is a contract of this mode). ``tier=2`` adds
    empty alternation branches to the grammar (the nullable-alternation class).
    """
    seen = set()
    for pattern, _size in _alts(k, alphabet, empty_branches=(tier >= 2)):
        seen.add(pattern)
    return sorted(seen)


def enumerate_inputs(n, alphabet="ab", extra="c"):
    """Return every string of length <= `n` over `alphabet`, plus each with one out-of-alphabet char.

    The out-of-alphabet character exercises negated classes and ``.`` against a byte the pattern's
    alphabet never mentions.
    """
    inputs = [""]
    for length in range(1, n + 1):
        inputs.extend("".join(combo) for combo in itertools.product(alphabet, repeat=length))
    if extra:
        base = list(inputs)
        for text in base:
            inputs.append(text + extra)
    # de-dup preserving determinism
    return sorted(set(inputs), key=lambda s: (len(s), s))


def count_inputs(n, alphabet="ab", extra="c"):
    """The closed-form size of :func:`enumerate_inputs`, for a completeness cross-check.

    Strings of length <= n over an `a`-letter alphabet number ``(a**(n+1) - 1) / (a - 1)``; appending one
    extra char to each (minus the collisions that were already present) is folded in by the set, so this
    returns the *pre-dedup* upper bound the enumerator is checked against by set size.
    """
    a = len(alphabet)
    base = (a ** (n + 1) - 1) // (a - 1) if a > 1 else n + 1
    return base if not extra else base * 2  # each base string, plus itself + extra


def enumerate_cases(k, n, alphabet="ab", extra="c"):
    """Yield (pattern, input) for the full cross product — the exhaustive case space."""
    patterns = enumerate_patterns(k, alphabet)
    inputs = enumerate_inputs(n, alphabet, extra)
    for pattern in patterns:
        for text in inputs:
            yield pattern, text


def as_cases(k, n, alphabet="ab", extra="c"):
    """Yield :class:`sciforge.corpus.Case` records for the space, ready for :func:`run_corpus`.

    Each case has no stored ``expected`` — the exhaustive mode judges the engine against a *freshly run*
    oracle (``python_re``), so ``out_of_contract`` never fires (its trigger needs a stored expectation)
    and every divergence is a genuine engine-vs-oracle difference passed to ``classify``.
    """
    from .schema import Case  # local import keeps the enumerator itself dependency-light
    for pattern, text in enumerate_cases(k, n, alphabet, extra):
        yield Case(pattern=pattern, input=text)
