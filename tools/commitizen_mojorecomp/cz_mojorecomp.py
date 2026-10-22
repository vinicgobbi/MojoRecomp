"""Commitizen extensions for MojoRecomp's independently versioned components.

MojoRecomp releases the launcher and each title runtime on separate version
lines from one repository. Commit messages decide the size of a version bump,
but commit scopes are not reliable enough to decide which component a commit
belongs to. This plugin therefore attributes commits by the paths they change:

- ``cz_mojorecomp`` is the Conventional Commits rule restricted to commits that
  touch ``extras.component_paths`` (git pathspecs). It drives both the bump
  increment and the changelog of one component. When none of those commits
  calls for a release, ``cz bump`` stops with NO_INCREMENT (exit code 21)
  instead of advancing a pre-release counter; pass ``--increment`` to force a
  release.
- ``mojorecomp-version-toml`` is a version provider that keeps ``version.toml``
  the source of truth. It reads the first key of ``extras.version_keys``,
  writes every listed key, and rewrites each ``extras.version_mirrors`` entry.

A mirror is ``{ file, pattern, count = 1 }``: ``pattern`` is a multi-line
regular expression whose ``version`` group is replaced with the new version,
and it must match exactly ``count`` times. Mirrors replace whatever version is
present instead of searching for the current version string, so a copy that
drifted, or a legacy spelling such as ``0.2.0-alpha``, is still updated.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from commitizen import git
from commitizen.cz.conventional_commits import ConventionalCommitsCz
from commitizen.exceptions import CommitizenException, ExitCode, NoneIncrementExit
from commitizen.providers.base_provider import VersionProvider
from commitizen.version_increment import VersionIncrement

VERSION_FILE = "version.toml"


class MojoRecompConfigError(CommitizenException):
    exit_code = ExitCode.INVALID_CONFIGURATION


def _extras(config) -> dict:
    return config.settings.get("extras") or {}


def _string_list(config, key: str) -> list[str]:
    value = _extras(config).get(key)
    if not isinstance(value, list) or not value or not all(isinstance(v, str) for v in value):
        raise MojoRecompConfigError(f"extras.{key} must be a non-empty list of strings")
    return value


def _commit_touches(rev: str, pathspecs: list[str]) -> bool:
    result = subprocess.run(
        ["git", "diff-tree", "--root", "--no-commit-id", "--name-only", "-r", "-m", rev, "--", *pathspecs],
        check=True,
        capture_output=True,
        text=True,
    )
    return bool(result.stdout.strip())


class MojoRecompCz(ConventionalCommitsCz):
    """Conventional Commits limited to the commits that change one component."""

    def filter_commits(self, commits: list[git.GitCommit]) -> list[git.GitCommit]:
        pathspecs = _string_list(self.config, "component_paths")
        return [commit for commit in commits if _commit_touches(commit.rev, pathspecs)]

    def filter_commits_before_bump(self, commits: list[git.GitCommit]) -> list[git.GitCommit]:
        selected = self.filter_commits(commits)
        bump_map = (
            self.bump_map_major_version_zero if self.config.settings["major_version_zero"] else self.bump_map
        )
        increment = VersionIncrement.get_highest_by_messages(
            (commit.message for commit in selected), self.bump_pattern, bump_map
        )
        if increment == VersionIncrement.NONE:
            # Commitizen would still advance a pre-release counter here.
            raise NoneIncrementExit("[NO_INCREMENT]\nNo commit since the last release of this component calls for a new version.")
        return selected


def _version_toml_pattern(key: str) -> str:
    return rf'^{re.escape(key)}\s*=\s*"(?P<version>[^"]*)"'


def _read(path: Path) -> str:
    # newline="" keeps line endings byte-identical apart from the version.
    with path.open(encoding="utf-8", newline="") as handle:
        return handle.read()


def _write(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        handle.write(text)


def _replace_versions(path: Path, pattern: str, version: str, count: int) -> None:
    text = _read(path)
    # Patterns are written with "\n"; match a CRLF checkout the same way.
    crlf = "\r\n" in text
    normalized = text.replace("\r\n", "\n") if crlf else text
    regex = re.compile(pattern, re.MULTILINE)
    matches = list(regex.finditer(normalized))
    if len(matches) != count:
        raise MojoRecompConfigError(
            f"Expected {count} version match(es) for /{pattern}/ in {path}, found {len(matches)}"
        )
    pieces = []
    position = 0
    for match in matches:
        start, end = match.span("version")
        pieces.append(normalized[position:start])
        pieces.append(version)
        position = end
    pieces.append(normalized[position:])
    updated = "".join(pieces)
    _write(path, updated.replace("\n", "\r\n") if crlf else updated)


class VersionTomlProvider(VersionProvider):
    """Reads and writes component versions in the repository's version.toml."""

    def get_version(self) -> str:
        key = _string_list(self.config, "version_keys")[0]
        match = re.search(_version_toml_pattern(key), _read(Path(VERSION_FILE)), re.MULTILINE)
        if not match:
            raise MojoRecompConfigError(f"{VERSION_FILE} has no '{key}' entry")
        return match["version"]

    def set_version(self, version: str) -> None:
        for key in _string_list(self.config, "version_keys"):
            _replace_versions(Path(VERSION_FILE), _version_toml_pattern(key), version, 1)
        for mirror in _extras(self.config).get("version_mirrors", []):
            _replace_versions(Path(mirror["file"]), mirror["pattern"], version, mirror.get("count", 1))
