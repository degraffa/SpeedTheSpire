#!/usr/bin/env python3
"""Fail-closed path and identity helpers for oracle campaign artifacts."""

from __future__ import annotations

import os
import re
import stat

_CAMPAIGN_ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
_SEED_RE = re.compile(r"[0-9A-Z]+")
ORACLE_LAUNCH_TOKEN_ENV = "SPEEDTHESPIRE_ORACLE_LAUNCH_TOKEN"


def validate_campaign_id(campaign_id: str) -> str:
    """Return a safe, single-component campaign id or raise ValueError."""
    if not isinstance(campaign_id, str) or \
            _CAMPAIGN_ID_RE.fullmatch(campaign_id) is None or \
            campaign_id in (".", "..") or \
            os.path.isabs(campaign_id) or \
            "/" in campaign_id or "\\" in campaign_id:
        raise ValueError(
            "campaign id must be one non-empty [A-Za-z0-9._-] path component")
    return campaign_id


def validate_seed_list(seeds: list) -> list:
    """Return validated uppercase seed strings with stable order."""
    if not isinstance(seeds, list) or not seeds:
        raise ValueError("seed list must be non-empty")
    normalized = []
    for seed in seeds:
        if not isinstance(seed, str):
            raise ValueError("seeds must be strings")
        value = seed.upper()
        if _SEED_RE.fullmatch(value) is None:
            raise ValueError(
                f"seed {seed!r} is not a non-empty base-35-style string")
        normalized.append(value)
    if len(normalized) != len(set(normalized)):
        raise ValueError("seed list contains duplicates")
    return normalized


def _contained(root: str, target: str) -> bool:
    try:
        return os.path.normcase(os.path.commonpath([root, target])) == \
            os.path.normcase(root)
    except ValueError:
        return False


def _same_path(left: str, right: str) -> bool:
    return os.path.normcase(os.path.normpath(left)) == \
        os.path.normcase(os.path.normpath(right))


def _is_redirect(path: str) -> bool:
    """Whether an existing path is a symlink/junction/reparse redirect."""
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        return False
    if stat.S_ISLNK(info.st_mode):
        return True
    # Python 3.9 has no os.path.isjunction(). Windows lstat exposes the same
    # fail-closed signal for symlinks, junctions, mount points, and other
    # name-surrogate reparse points.
    reparse = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return bool(getattr(info, "st_file_attributes", 0) & reparse)


def exact_path_without_redirect(path: str) -> str:
    """Return an absolute lexical path only when no component redirects it."""
    exact = os.path.abspath(path)
    if _is_redirect(exact):
        raise ValueError(f"refusing redirected campaign path: {exact!r}")
    resolved = os.path.realpath(exact)
    if not _same_path(exact, resolved):
        raise ValueError(
            f"refusing redirected campaign path: {exact!r} resolves to "
            f"{resolved!r}")
    return exact


def campaign_dir_under_root(data_root: str, campaign_id: str) -> str:
    """Resolve a campaign directory and prove it stays below data_root.

    realpath catches an existing campaign-directory symlink that redirects
    writes or cleanup outside the operator-selected data root.
    """
    validate_campaign_id(campaign_id)
    root = os.path.realpath(os.path.abspath(data_root))
    target = exact_path_without_redirect(os.path.join(root, campaign_id))
    if target == root or not _contained(root, target):
        raise ValueError(
            f"campaign directory escapes data root: {target!r} not below "
            f"{root!r}")
    return target


def campaign_file_under_root(data_root: str, campaign_id: str,
                             name: str) -> str:
    """Resolve one direct campaign child and prove its real path is contained."""
    if not isinstance(name, str) or not name or \
            os.path.basename(name) != name or name in (".", ".."):
        raise ValueError(f"unsafe campaign file name {name!r}")
    directory = campaign_dir_under_root(data_root, campaign_id)
    target = exact_path_without_redirect(os.path.join(directory, name))
    if target == directory or not _contained(directory, target):
        raise ValueError(
            f"campaign file escapes campaign directory: {target!r} not below "
            f"{directory!r}")
    return target
