import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--update-gold",
        action="store_true",
        help="rewrite gold transcripts from the current output instead of comparing",
    )


@pytest.fixture
def update_gold(request: pytest.FixtureRequest) -> bool:
    return bool(request.config.getoption("--update-gold"))
