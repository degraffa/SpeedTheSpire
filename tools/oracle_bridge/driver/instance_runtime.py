#!/usr/bin/env python3
"""Prepare an isolated, resumable filesystem for one oracle game instance.

ModTheSpire reads config from ``LOCALAPPDATA`` while Slay the Spire and
CommunicationMod write saves, preferences, run history, and the child stderr
log relative to the JVM working directory.  ModTheSpire also unpacks a jar
below ``java.io.tmpdir``.  A campaign therefore needs all three namespaces;
isolating only its artifacts is not sufficient.

This module is deliberately stdlib-only.  It creates a fixed profile template
once, then gives each campaign a private copy plus private config/temp/log
roots.  Runtime manifests make resume fail loudly if an immutable input or
runtime file drifts.  Mutable game files (the campaign's private preferences,
saves, and run history) persist across game relaunches and campaign resume.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import uuid
from dataclasses import dataclass
from typing import Any, Dict, Tuple

from campaign_paths import (
    campaign_dir_under_root,
    campaign_file_under_root,
    exact_path_without_redirect,
    validate_campaign_id,
)


RUNTIME_FORMAT = 1
PROFILE_TEMPLATE_FORMAT = 1
PROFILE_TEMPLATE_ID = "profile-template-v1"
RUNTIME_MANIFEST = "runtime_manifest.json"
PROFILE_MANIFEST = "profile_manifest.json"

DESKTOP_JAR = "desktop-1.0.jar"
FORK_JAR = "CommunicationMod-oracle.jar"

# config.json is used by the native launcher rather than this direct JVM
# launch, but copying the tiny frozen root configuration keeps a runtime
# inspectable and self-contained.  Every entry is optional: old/local game
# installs do not necessarily carry all four.  The game rewrites its display
# and Twitch settings, whereas the launch config and Steam app id are pinned
# inputs and must remain byte-identical for a resumable runtime.
IMMUTABLE_ROOT_COPY_FILES = (
    "config.json",
    "steam_appid.txt",
)
MUTABLE_ROOT_COPY_FILES = (
    "info.displayconfig",
    "twitchconfig.txt",
)
ROOT_COPY_FILES = IMMUTABLE_ROOT_COPY_FILES + MUTABLE_ROOT_COPY_FILES

# These are the profile files the frozen oracle environment relies on.  The
# source profile was audited as fully unlocked at B0.2; checking the required
# files plus the STSUnlocks values prevents accidentally templating a blank or
# partially-written profile while retaining that audit as the source of truth.
REQUIRED_PROFILE_FILES = (
    "STSSaveSlots",
    "STSUnlocks",
    "STSUnlockProgress",
    "STSDataVagabond",
    "STSPlayer",
)

# Pool gates enumerated by UnlockTracker.refresh() in the audited game source:
# 36 addCard calls followed by 24 addRelic calls.  Its three addCharacter
# calls are intentionally not part of this contract: the B0.2 audit proves
# the card/relic run pools complete via lockedCards/lockedRelics.  Other
# STSUnlocks keys are legitimate profile state and remain allowed below.
REQUIRED_POOL_UNLOCK_KEYS = (
    "Havoc",
    "Sentinel",
    "Exhume",
    "Wild Strike",
    "Evolve",
    "Immolate",
    "Heavy Blade",
    "Spot Weakness",
    "Limit Break",
    "Concentrate",
    "Setup",
    "Grand Finale",
    "Cloak And Dagger",
    "Accuracy",
    "Storm of Steel",
    "Bane",
    "Catalyst",
    "Corpse Explosion",
    "Rebound",
    "Undo",
    "Echo Form",
    "Turbo",
    "Sunder",
    "Meteor Strike",
    "Hyperbeam",
    "Recycle",
    "Core Surge",
    "Prostrate",
    "Blasphemy",
    "Devotion",
    "ForeignInfluence",
    "Alpha",
    "MentalFortress",
    "SpiritShield",
    "Wish",
    "Wireheading",
    "Omamori",
    "Prayer Wheel",
    "Shovel",
    "Art of War",
    "The Courier",
    "Pandora's Box",
    "Blue Candle",
    "Dead Branch",
    "Singing Bowl",
    "Du-Vu Doll",
    "Smiling Mask",
    "Tiny Chest",
    "Cables",
    "DataDisk",
    "Emotion Chip",
    "Runic Capacitor",
    "Turnip",
    "Symbiotic Virus",
    "Akabeko",
    "Yang",
    "CeramicFish",
    "StrikeDummy",
    "TeardropLocket",
    "CloakClasp",
)


@dataclass(frozen=True)
class InstanceRuntime:
    """Plain-string paths and identity metadata for one oracle JVM."""

    campaign_id: str
    runtime_dir: str
    game_workdir: str
    local_app_data: str
    app_data: str
    temp_dir: str
    logs_dir: str
    fork_jar: str
    communication_log: str
    manifest_path: str
    manifest_sha256: str
    runtime_format: int
    desktop_sha256: str
    fork_sha256: str
    profile_template_sha256: str
    profile_file_digests: Dict[str, str]
    manifest_metadata: Dict[str, Any]


def _normalized_path(path: str) -> str:
    return os.path.normcase(os.path.normpath(os.path.abspath(path)))


def _contained(root: str, target: str) -> bool:
    try:
        return os.path.normcase(os.path.commonpath([root, target])) == \
            os.path.normcase(root)
    except ValueError:
        return False


def _managed_child(root: str, *parts: str) -> str:
    root = exact_path_without_redirect(root)
    target = os.path.abspath(os.path.join(root, *parts))
    if target == root or not _contained(root, target):
        raise ValueError(
            f"instance runtime path escapes root: {target!r} not below "
            f"{root!r}")
    # This also catches a redirect in any existing parent component.  A final
    # component that does not exist yet is safe because its real path remains
    # lexical until it is created.
    return exact_path_without_redirect(target)


def _sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            block = fh.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _is_redirect(path: str) -> bool:
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        return False
    if stat.S_ISLNK(info.st_mode):
        return True
    reparse = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return bool(getattr(info, "st_file_attributes", 0) & reparse)


def _assert_regular_tree(root: str) -> str:
    """Return an exact directory after rejecting every redirect/special file."""
    root = exact_path_without_redirect(root)
    if not os.path.isdir(root):
        raise ValueError(f"expected directory: {root!r}")
    for current, directories, files in os.walk(root, topdown=True,
                                               followlinks=False):
        exact_path_without_redirect(current)
        for name in directories:
            path = os.path.join(current, name)
            if _is_redirect(path):
                raise ValueError(f"refusing redirected runtime path: {path!r}")
            info = os.lstat(path)
            if not stat.S_ISDIR(info.st_mode):
                raise ValueError(f"refusing special runtime path: {path!r}")
            exact_path_without_redirect(path)
        for name in files:
            path = os.path.join(current, name)
            if _is_redirect(path):
                raise ValueError(f"refusing redirected runtime path: {path!r}")
            info = os.lstat(path)
            if not stat.S_ISREG(info.st_mode):
                raise ValueError(f"refusing special runtime path: {path!r}")
            exact_path_without_redirect(path)
    return root


def _tree_inventory(root: str) -> Tuple[str, Dict[str, Dict[str, Any]]]:
    """Return a deterministic tree digest and per-file metadata."""
    root = _assert_regular_tree(root)
    files: Dict[str, Dict[str, Any]] = {}
    for current, directories, names in os.walk(root, topdown=True,
                                               followlinks=False):
        directories.sort()
        for name in sorted(names):
            path = os.path.join(current, name)
            relative = os.path.relpath(path, root).replace(os.sep, "/")
            files[relative] = {
                "sha256": _sha256_file(path),
                "size": os.path.getsize(path),
            }
    digest = hashlib.sha256()
    for relative, metadata in files.items():
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(metadata["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(metadata["sha256"].encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest(), files


def _read_json(path: str) -> Dict[str, Any]:
    try:
        with open(path, "r", encoding="utf-8") as fh:
            value = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read runtime manifest {path!r}: {exc}") \
            from exc
    if not isinstance(value, dict):
        raise ValueError(f"runtime manifest is not an object: {path!r}")
    return value


def _write_json(path: str, value: Dict[str, Any]) -> None:
    with open(path, "x", encoding="utf-8", newline="\n") as fh:
        json.dump(value, fh, indent=2, sort_keys=True)
        fh.write("\n")


def _source_file(path: str, label: str) -> str:
    path = exact_path_without_redirect(path)
    if not os.path.isfile(path) or not stat.S_ISREG(os.lstat(path).st_mode):
        raise ValueError(f"missing {label}: {path!r}")
    return path


def _validate_unlocked_profile(preferences: str) -> None:
    for name in REQUIRED_PROFILE_FILES:
        _source_file(os.path.join(preferences, name),
                     f"oracle profile file {name}")
    unlocks_path = os.path.join(preferences, "STSUnlocks")
    try:
        with open(unlocks_path, "r", encoding="utf-8") as fh:
            unlocks = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid oracle STSUnlocks: {exc}") from exc
    if not isinstance(unlocks, dict) or not unlocks:
        raise ValueError("oracle STSUnlocks must be a non-empty object")
    missing = [key for key in REQUIRED_POOL_UNLOCK_KEYS
               if key not in unlocks]
    if missing:
        preview = ", ".join(missing[:5])
        raise ValueError(
            "oracle profile is missing required STSUnlocks pool entries: "
            f"{preview}")
    locked = sorted(str(key) for key, value in unlocks.items()
                    if str(value) != "2")
    if locked:
        preview = ", ".join(locked[:5])
        raise ValueError(
            "oracle profile is not fully unlocked; non-2 STSUnlocks entries: "
            f"{preview}")


def _remove_managed_tree(path: str, runtime_root: str) -> None:
    root = exact_path_without_redirect(runtime_root)
    path = exact_path_without_redirect(path)
    if path == root or not _contained(root, path):
        raise ValueError(f"refusing runtime cleanup outside root: {path!r}")
    if not os.path.exists(path):
        return
    _assert_regular_tree(path)
    shutil.rmtree(path)


def _profile_template(runtime_root: str, source_game_dir: str) \
        -> Tuple[str, Dict[str, Any], str]:
    """Create once or verify the shared immutable profile template."""
    template_dir = campaign_dir_under_root(runtime_root, PROFILE_TEMPLATE_ID)
    source_preferences = _assert_regular_tree(
        os.path.join(source_game_dir, "preferences"))
    _validate_unlocked_profile(source_preferences)
    source_identity = {
        "game_dir": _normalized_path(source_game_dir),
        "preferences": _normalized_path(source_preferences),
    }

    if not os.path.exists(template_dir):
        source_tree_sha256, source_files = _tree_inventory(source_preferences)
        staging_id = (f"{PROFILE_TEMPLATE_ID}.tmp-{os.getpid()}-"
                      f"{uuid.uuid4().hex}")
        staging = campaign_dir_under_root(runtime_root, staging_id)
        try:
            os.mkdir(staging)
            staged_preferences = _managed_child(staging, "preferences")
            shutil.copytree(source_preferences, staged_preferences,
                            copy_function=shutil.copy2)
            captured_sha256, captured_files = _tree_inventory(
                staged_preferences)
            after_sha256, after_files = _tree_inventory(source_preferences)
            if (source_tree_sha256 != after_sha256 or
                    source_files != after_files or
                    captured_sha256 != source_tree_sha256 or
                    captured_files != source_files):
                raise ValueError(
                    "source oracle profile changed during template snapshot; "
                    "retry after the source game is stopped")
            manifest = {
                "profile_template_format": PROFILE_TEMPLATE_FORMAT,
                "source": source_identity,
                "tree_sha256": captured_sha256,
                "files": captured_files,
            }
            _write_json(os.path.join(staging, PROFILE_MANIFEST), manifest)
            try:
                os.rename(staging, template_dir)
            except OSError:
                # A concurrent creator may have installed the same immutable
                # template first.  Verify that winner below rather than merge.
                if not os.path.isdir(template_dir):
                    raise
        finally:
            if os.path.exists(staging):
                _remove_managed_tree(staging, runtime_root)

    _assert_regular_tree(template_dir)
    manifest_path = _managed_child(template_dir, PROFILE_MANIFEST)
    manifest = _read_json(manifest_path)
    if manifest.get("profile_template_format") != PROFILE_TEMPLATE_FORMAT:
        raise ValueError("oracle profile template format drift")
    if manifest.get("source") != source_identity:
        raise ValueError("oracle profile template source path drift")
    template_preferences = _assert_regular_tree(
        _managed_child(template_dir, "preferences"))
    _validate_unlocked_profile(template_preferences)
    template_sha256, template_files = _tree_inventory(template_preferences)
    if (manifest.get("tree_sha256") != template_sha256 or
            manifest.get("files") != template_files):
        raise ValueError("oracle profile template byte drift")
    # The source path and unlock contract stay bound, but ordinary live-profile
    # statistics are expected to change after the first snapshot.  The
    # template's own bytes above, not the mutable source tree, are the resume
    # identity.
    return template_dir, manifest, _sha256_file(manifest_path)


def _root_source_files(source_game_dir: str, names: Tuple[str, ...]) \
        -> Dict[str, Dict[str, Any]]:
    result: Dict[str, Dict[str, Any]] = {}
    for name in names:
        path = os.path.join(source_game_dir, name)
        if not os.path.exists(path):
            continue
        path = _source_file(path, f"game root file {name}")
        result[name] = {
            "sha256": _sha256_file(path),
            "size": os.path.getsize(path),
        }
    return result


def _verify_root_copies(game_workdir: str,
                        files: Dict[str, Dict[str, Any]],
                        label: str) -> None:
    for name, expected in files.items():
        path = _source_file(os.path.join(game_workdir, name),
                            f"runtime root file {name}")
        actual = {
            "sha256": _sha256_file(path),
            "size": os.path.getsize(path),
        }
        if actual != expected:
            raise ValueError(
                f"oracle instance runtime {label} root-file drift: {name}")


def _runtime_paths(runtime_dir: str) -> Dict[str, str]:
    game = _managed_child(runtime_dir, "game")
    return {
        "runtime_dir": runtime_dir,
        "game_workdir": game,
        "local_app_data": _managed_child(runtime_dir, "localappdata"),
        "app_data": _managed_child(runtime_dir, "appdata"),
        "temp_dir": _managed_child(runtime_dir, "tmp"),
        "logs_dir": _managed_child(runtime_dir, "logs"),
        "fork_jar": _managed_child(game, "mods", FORK_JAR),
        "communication_log": _managed_child(
            game, "communication_mod_errors.log"),
        "manifest_path": _managed_child(runtime_dir, RUNTIME_MANIFEST),
        "desktop_jar": _managed_child(game, DESKTOP_JAR),
        "preferences": _managed_child(game, "preferences"),
    }


def _build_runtime(staging: str, final_runtime_dir: str, campaign_id: str,
                   source_game_dir: str, source_desktop: str,
                   source_fork_jar: str, desktop_sha256: str,
                   fork_sha256: str, root_source_files: Dict[str, Dict[str, Any]],
                   template_dir: str, template_manifest: Dict[str, Any],
                   template_manifest_sha256: str) -> None:
    stage_paths = _runtime_paths(staging)
    final_paths = _runtime_paths(final_runtime_dir)
    os.mkdir(stage_paths["game_workdir"])
    for key in ("local_app_data", "app_data", "temp_dir", "logs_dir"):
        os.mkdir(stage_paths[key])
    os.mkdir(os.path.join(stage_paths["game_workdir"], "mods"))
    for name in ("saves", "runs", "sendToDevs"):
        os.mkdir(os.path.join(stage_paths["game_workdir"], name))

    desktop_link = "hardlink"
    try:
        os.link(source_desktop, stage_paths["desktop_jar"])
    except OSError:
        desktop_link = "copy"
        shutil.copy2(source_desktop, stage_paths["desktop_jar"])
    shutil.copy2(source_fork_jar, stage_paths["fork_jar"])
    for category in ("immutable", "mutable_initial"):
        for name in root_source_files[category]:
            shutil.copy2(os.path.join(source_game_dir, name),
                         os.path.join(stage_paths["game_workdir"], name))
    # Pin provenance to what was actually materialized, not merely to the
    # source bytes observed before copy.  Mutable files may evolve only after
    # this initial fidelity check.
    _verify_root_copies(
        stage_paths["game_workdir"], root_source_files["immutable"],
        "immutable-copy")
    _verify_root_copies(
        stage_paths["game_workdir"], root_source_files["mutable_initial"],
        "initial-copy")
    shutil.copytree(os.path.join(template_dir, "preferences"),
                    stage_paths["preferences"], copy_function=shutil.copy2)

    manifest = {
        "runtime_format": RUNTIME_FORMAT,
        "campaign_id": campaign_id,
        "sources": {
            "game_dir": _normalized_path(source_game_dir),
            "desktop_jar": _normalized_path(source_desktop),
            "fork_jar": _normalized_path(source_fork_jar),
        },
        "source_hashes": {
            "desktop_jar": desktop_sha256,
            "fork_jar": fork_sha256,
        },
        "root_source_files": root_source_files,
        "profile_template": {
            "path": _normalized_path(template_dir),
            "manifest_sha256": template_manifest_sha256,
            "tree_sha256": template_manifest["tree_sha256"],
            "files": template_manifest["files"],
        },
        "paths": {key: _normalized_path(value)
                  for key, value in final_paths.items()
                  if key not in ("preferences", "desktop_jar")},
        "runtime_immutable": {
            "desktop_jar": desktop_sha256,
            "desktop_materialization": desktop_link,
            "fork_jar": fork_sha256,
        },
    }
    _write_json(stage_paths["manifest_path"], manifest)


def _load_runtime(runtime_dir: str, campaign_id: str,
                  source_game_dir: str, source_desktop: str,
                  source_fork_jar: str, desktop_sha256: str,
                  fork_sha256: str,
                  root_source_files: Dict[str, Dict[str, Any]],
                  template_dir: str, template_manifest: Dict[str, Any],
                  template_manifest_sha256: str) -> InstanceRuntime:
    _assert_regular_tree(runtime_dir)
    paths = _runtime_paths(runtime_dir)
    manifest = _read_json(paths["manifest_path"])
    expected_sources = {
        "game_dir": _normalized_path(source_game_dir),
        "desktop_jar": _normalized_path(source_desktop),
        "fork_jar": _normalized_path(source_fork_jar),
    }
    expected_hashes = {
        "desktop_jar": desktop_sha256,
        "fork_jar": fork_sha256,
    }
    expected_profile = {
        "path": _normalized_path(template_dir),
        "manifest_sha256": template_manifest_sha256,
        "tree_sha256": template_manifest["tree_sha256"],
        "files": template_manifest["files"],
    }
    expected_paths = {key: _normalized_path(value)
                      for key, value in paths.items()
                      if key not in ("preferences", "desktop_jar")}
    if manifest.get("runtime_format") != RUNTIME_FORMAT:
        raise ValueError("oracle instance runtime format drift")
    if manifest.get("campaign_id") != campaign_id:
        raise ValueError("oracle instance runtime campaign identity drift")
    if manifest.get("sources") != expected_sources:
        raise ValueError("oracle instance runtime source path drift")
    if manifest.get("source_hashes") != expected_hashes:
        raise ValueError("oracle instance runtime source byte drift")
    root_manifest = manifest.get("root_source_files")
    if not isinstance(root_manifest, dict) or \
            set(root_manifest) != {"immutable", "mutable_initial"} or \
            not isinstance(root_manifest["immutable"], dict) or \
            not isinstance(root_manifest["mutable_initial"], dict) or \
            not set(root_manifest["immutable"]).issubset(
                IMMUTABLE_ROOT_COPY_FILES) or \
            not set(root_manifest["mutable_initial"]).issubset(
                MUTABLE_ROOT_COPY_FILES):
        raise ValueError("oracle instance runtime root-source manifest drift")
    if root_manifest["immutable"] != root_source_files["immutable"]:
        raise ValueError("oracle instance runtime immutable root-source drift")
    if manifest.get("profile_template") != expected_profile:
        raise ValueError("oracle instance runtime profile-template drift")
    if manifest.get("paths") != expected_paths:
        raise ValueError("oracle instance runtime path drift")

    for key in ("game_workdir", "local_app_data", "app_data", "temp_dir",
                "logs_dir"):
        if not os.path.isdir(paths[key]):
            raise ValueError(f"oracle instance runtime missing {key}")
    # Preferences are private mutable campaign state, so they are not pinned
    # byte-for-byte to the template.  The pool-unlock invariant is different:
    # a truncated/corrupted STSUnlocks would silently change game rules and
    # must fail before this runtime can be launched or resumed.
    _validate_unlocked_profile(paths["preferences"])
    _verify_root_copies(
        paths["game_workdir"], root_manifest["immutable"], "immutable")
    for key in ("desktop_jar", "fork_jar"):
        _source_file(paths[key], f"runtime {key}")
    if _sha256_file(paths["desktop_jar"]) != desktop_sha256:
        raise ValueError("oracle instance runtime desktop jar byte drift")
    if _sha256_file(paths["fork_jar"]) != fork_sha256:
        raise ValueError("oracle instance runtime fork jar byte drift")
    immutable = manifest.get("runtime_immutable")
    if not isinstance(immutable, dict) or \
            immutable.get("desktop_jar") != desktop_sha256 or \
            immutable.get("fork_jar") != fork_sha256 or \
            immutable.get("desktop_materialization") not in \
            ("hardlink", "copy"):
        raise ValueError("oracle instance runtime immutable manifest drift")

    profile_files = {
        relative: metadata["sha256"]
        for relative, metadata in template_manifest["files"].items()
    }
    return InstanceRuntime(
        campaign_id=campaign_id,
        runtime_dir=runtime_dir,
        game_workdir=paths["game_workdir"],
        local_app_data=paths["local_app_data"],
        app_data=paths["app_data"],
        temp_dir=paths["temp_dir"],
        logs_dir=paths["logs_dir"],
        fork_jar=paths["fork_jar"],
        communication_log=paths["communication_log"],
        manifest_path=paths["manifest_path"],
        manifest_sha256=_sha256_file(paths["manifest_path"]),
        runtime_format=RUNTIME_FORMAT,
        desktop_sha256=desktop_sha256,
        fork_sha256=fork_sha256,
        profile_template_sha256=template_manifest["tree_sha256"],
        profile_file_digests=profile_files,
        manifest_metadata=manifest,
    )


def prepare_instance_runtime(campaign_id: str, source_game_dir: str,
                             source_fork_jar: str, runtime_root: str,
                             fresh: bool = False) -> InstanceRuntime:
    """Create or verify one campaign's isolated game runtime.

    ``fresh`` resets only this validated campaign runtime.  The shared profile
    template remains immutable, so a fresh attempt cannot silently change the
    campaign environment.  Existing mutable private game state is otherwise
    preserved for relaunch and resume.
    """
    validate_campaign_id(campaign_id)
    if campaign_id == PROFILE_TEMPLATE_ID or ".tmp-" in campaign_id:
        raise ValueError("campaign id is reserved by instance runtime storage")
    if not isinstance(fresh, bool):
        raise ValueError("fresh must be boolean")

    # Reject a lexical runtime root that reaches storage through any junction
    # or symlink; campaign_paths then proves every direct campaign directory is
    # contained below that exact root.
    runtime_root = os.path.abspath(runtime_root)
    os.makedirs(runtime_root, exist_ok=True)
    runtime_root = exact_path_without_redirect(runtime_root)
    runtime_dir = campaign_dir_under_root(runtime_root, campaign_id)

    source_game_dir = exact_path_without_redirect(source_game_dir)
    if not os.path.isdir(source_game_dir):
        raise ValueError(f"missing source game directory: {source_game_dir!r}")
    source_desktop = _source_file(
        os.path.join(source_game_dir, DESKTOP_JAR), "desktop game jar")
    source_fork_jar = _source_file(source_fork_jar, "oracle fork jar")
    desktop_sha256 = _sha256_file(source_desktop)
    fork_sha256 = _sha256_file(source_fork_jar)
    immutable_root_files = _root_source_files(
        source_game_dir, IMMUTABLE_ROOT_COPY_FILES)
    will_build = fresh or not os.path.exists(runtime_dir)
    root_source_files = {
        "immutable": immutable_root_files,
        # Do not even inspect game-written source settings during resume: the
        # runtime's private copies and their source counterparts may both have
        # evolved legitimately since the initial snapshot.
        "mutable_initial": (_root_source_files(
            source_game_dir, MUTABLE_ROOT_COPY_FILES) if will_build else {}),
    }

    template_dir, template_manifest, template_manifest_sha256 = \
        _profile_template(runtime_root, source_game_dir)

    if fresh and os.path.exists(runtime_dir):
        _remove_managed_tree(runtime_dir, runtime_root)

    if not os.path.exists(runtime_dir):
        staging_id = (f"{campaign_id}.tmp-{os.getpid()}-"
                      f"{uuid.uuid4().hex}")
        staging = campaign_dir_under_root(runtime_root, staging_id)
        try:
            os.mkdir(staging)
            _build_runtime(
                staging, runtime_dir, campaign_id, source_game_dir,
                source_desktop, source_fork_jar, desktop_sha256, fork_sha256,
                root_source_files, template_dir, template_manifest,
                template_manifest_sha256)
            os.rename(staging, runtime_dir)
        finally:
            if os.path.exists(staging):
                _remove_managed_tree(staging, runtime_root)

    return _load_runtime(
        runtime_dir, campaign_id, source_game_dir, source_desktop,
        source_fork_jar, desktop_sha256, fork_sha256, root_source_files,
        template_dir, template_manifest, template_manifest_sha256)
