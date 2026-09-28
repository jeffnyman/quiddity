"""End-to-end smoke test: hand-written Dialog story -> dgdebug transcript -> gold file.

This is the regression pattern the Quiddity compiler will use once it emits Dialog:
compile .qd -> .dg, run the walkthrough, diff against gold. Regenerate gold with:
    uv run pytest --update-gold
"""

from pathlib import Path

import pytest

from quiddity import toolchain

FIXTURES = Path(__file__).parent / "fixtures"


@pytest.fixture(scope="session", autouse=True)
def _toolchain_present():
    # These tests only need Dialog's tools, not aamachine's.
    dialog = next(r for r in toolchain.check().repos if r.repo is toolchain.DIALOG)

    if not dialog.ok:
        pytest.skip('Dialog tools not built; run "quiddity build"')


def test_chest_compiles_to_both_targets(tmp_path):
    sources = [FIXTURES / "chest.dg"]

    for fmt, ext in (("aa", ".aastory"), ("z8", ".z8")):
        out = tmp_path / f"chest{ext}"

        assert toolchain.compile(sources, out, fmt) == 0
        assert out.stat().st_size > 1000


def test_chest_walkthrough(update_gold):
    sources = [FIXTURES / "chest.dg", FIXTURES / "no-banner.dg"]
    got = toolchain.run_transcript(sources, (FIXTURES / "walkthrough.in").read_text())
    gold = FIXTURES / "chest.gold"

    if update_gold or not gold.exists():
        gold.write_text(got, newline="\n")
        pytest.skip(f"gold written: {gold}")

    assert got == gold.read_text()
