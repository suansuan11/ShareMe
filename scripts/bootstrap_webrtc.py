#!/usr/bin/env python3

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Sequence


DEPOT_TOOLS_URL = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
LOCK_KEYS = {"revision", "targets", "gnArgs"}
REQUIRED_HEADERS = (
    "api/peer_connection_interface.h",
    "api/create_modular_peer_connection_factory.h",
    "modules/audio_device/include/test_audio_device.h",
)
PUBLIC_INCLUDE_PATHS = (
    ".",
    "out/shareme/gen",
    "third_party/abseil-cpp",
    "third_party/libyuv/include",
    "third_party/perfetto/include",
    "out/shareme/gen/third_party/perfetto/build_config",
    "out/shareme/gen/third_party/perfetto",
)
LIBRARY_ROLES = (
    "adaptedVideoTrackSource",
    "testAudioDeviceModule",
    "webrtc",
)


def load_lock(path: Path) -> Dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("WebRTC lock file is unreadable or invalid") from error

    if not isinstance(value, dict):
        raise ValueError("WebRTC lock file must contain an object")

    unknown_keys = set(value) - LOCK_KEYS
    if unknown_keys:
        raise ValueError("WebRTC lock file contains unsupported keys")
    if set(value) != LOCK_KEYS:
        raise ValueError("WebRTC lock file is missing required keys")

    revision = value["revision"]
    if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("WebRTC revision must be a 40-character lowercase hash")

    targets = value["targets"]
    gn_args = value["gnArgs"]
    if (
        not isinstance(targets, list)
        or not targets
        or not all(isinstance(item, str) and item for item in targets)
    ):
        raise ValueError("WebRTC targets must be a non-empty string list")
    if (
        not isinstance(gn_args, list)
        or not gn_args
        or not all(isinstance(item, str) and item for item in gn_args)
    ):
        raise ValueError("WebRTC GN arguments must be a non-empty string list")

    return value


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def create_plan(repo_root: Path, external_root: Path) -> Dict[str, Any]:
    resolved_repo = repo_root.resolve()
    resolved_external = external_root.resolve()
    if resolved_external == resolved_repo or _is_relative_to(
        resolved_external, resolved_repo
    ):
        raise ValueError("WebRTC dependency root must stay outside the repository")

    lock = load_lock(resolved_repo / "deps/webrtc.lock.json")
    checkout_root = resolved_external / "checkout"
    source_root = checkout_root / "src"
    output_root = source_root / "out" / "shareme"
    return {
        "revision": lock["revision"],
        "externalRoot": str(resolved_external),
        "depotTools": str(resolved_external / "depot_tools"),
        "checkoutRoot": str(checkout_root),
        "sourceRoot": str(source_root),
        "outputRoot": str(output_root),
        "manifest": str(resolved_external / "shareme-webrtc-manifest.json"),
        "targets": lock["targets"],
        "gnArgs": lock["gnArgs"],
    }


def make_manifest(
    *,
    revision: str,
    system: str,
    architecture: str,
    include_dirs: Sequence[str],
    libraries: Sequence[str],
    compile_definitions: Sequence[str],
    gn_args: Sequence[str],
    msvc_runtime_library: str,
) -> Dict[str, Any]:
    if len(libraries) != len(LIBRARY_ROLES):
        raise ValueError("WebRTC manifest requires exactly three library roles")
    return {
        "revision": revision,
        "system": system,
        "architecture": architecture,
        "includeDirs": list(include_dirs),
        "libraries": [
            {"role": role, "path": path}
            for role, path in zip(LIBRARY_ROLES, libraries)
        ],
        "compileDefinitions": list(compile_definitions),
        "gnArgs": list(gn_args),
        "msvcRuntimeLibrary": msvc_runtime_library,
    }


def _run(command: Sequence[str], *, cwd: Path, env: Dict[str, str]) -> None:
    # Windows subprocess does not always resolve depot_tools .bat wrappers
    # (gn.bat/autoninja.bat/gclient.bat) the same way as an interactive CMD.
    # Explicitly use the batch entry points on Windows.
    normalized_command = list(command)
    if os.name == "nt" and normalized_command:
        windows_batch_tools = {"gn", "autoninja", "gclient"}
        if normalized_command[0] in windows_batch_tools:
            normalized_command[0] = normalized_command[0] + ".bat"

    subprocess.run(normalized_command, cwd=cwd, env=env, check=True)


def _tool_environment(depot_tools: Path) -> Dict[str, str]:
    environment = os.environ.copy()
    environment["PATH"] = str(depot_tools) + os.pathsep + environment.get("PATH", "")
    environment["DEPOT_TOOLS_UPDATE"] = "0"
    return environment


def _prepare(plan: Dict[str, Any]) -> None:
    external_root = Path(plan["externalRoot"])
    depot_tools = Path(plan["depotTools"])
    checkout_root = Path(plan["checkoutRoot"])
    source_root = Path(plan["sourceRoot"])
    external_root.mkdir(parents=True, exist_ok=True)

    if not depot_tools.exists():
        _run(
            ["git", "clone", "--depth", "1", DEPOT_TOOLS_URL, str(depot_tools)],
            cwd=external_root,
            env=os.environ.copy(),
        )
    elif not (depot_tools / ".git").exists():
        raise RuntimeError("existing depot_tools path is not a Git checkout")

    environment = _tool_environment(depot_tools)
    if not checkout_root.exists():
        checkout_root.mkdir()
        _run(
            ["fetch", "--nohooks", "--no-history", "webrtc"],
            cwd=checkout_root,
            env=environment,
        )
    elif not (checkout_root / ".gclient").exists():
        raise RuntimeError("existing checkout path is not a gclient workspace")

    if not (source_root / ".git").exists():
        raise RuntimeError("WebRTC source checkout is incomplete")

    _run(
        ["git", "fetch", "origin", plan["revision"]],
        cwd=source_root,
        env=environment,
    )
    _run(
        ["git", "checkout", "--detach", plan["revision"]],
        cwd=source_root,
        env=environment,
    )
    _run(
        [
            "gclient",
            "sync",
            "--jobs=4",
            "--revision",
            "src@" + plan["revision"],
        ],
        cwd=checkout_root,
        env=environment,
    )


def _library_paths(output_root: Path, system: str) -> List[Path]:
    if system == "Windows":
        return [
            output_root / "obj" / "api" / "video" / "adapted_video_track_source.lib",
            output_root
            / "obj"
            / "modules"
            / "audio_device"
            / "test_audio_device_module.lib",
            output_root / "obj" / "webrtc.lib",
        ]
    return [
        output_root / "obj" / "api" / "video" / "libadapted_video_track_source.a",
        output_root
        / "obj"
        / "modules"
        / "audio_device"
        / "libtest_audio_device_module.a",
        output_root / "obj" / "libwebrtc.a",
    ]


def _compile_definitions(system: str) -> List[str]:
    if system == "Windows":
        return [
            "NDEBUG",
            "WEBRTC_WIN",
            "NOMINMAX",
            "WIN32_LEAN_AND_MEAN",
        ]
    if system == "Darwin":
        return ["NDEBUG", "WEBRTC_POSIX", "WEBRTC_MAC"]
    if system == "Linux":
        return ["NDEBUG", "WEBRTC_POSIX", "WEBRTC_LINUX"]
    raise RuntimeError("unsupported WebRTC build platform")


def _msvc_runtime_library(system: str) -> str:
    if system == "Windows":
        return "MultiThreaded"
    return ""


def _materialize_linkable_libraries(
    libraries: Sequence[Path],
    *,
    source_root: Path,
    output_root: Path,
    system: str,
    environment: Dict[str, str],
) -> List[Path]:
    if system == "Windows":
        return list(libraries)

    llvm_ar = (
        source_root
        / "third_party"
        / "llvm-build"
        / "Release+Asserts"
        / "bin"
        / "llvm-ar"
    )
    linkable_libraries = []
    for archive in libraries:
        with archive.open("rb") as archive_file:
            archive_signature = archive_file.read(8)
        if archive_signature != b"!<thin>\n":
            linkable_libraries.append(archive)
            continue
        if not llvm_ar.is_file():
            raise RuntimeError("WebRTC LLVM archive tool is unavailable")

        listing = subprocess.run(
            [str(llvm_ar), "t", str(archive)],
            cwd=output_root,
            env=environment,
            check=True,
            capture_output=True,
            text=True,
        )
        members = []
        for entry in listing.stdout.splitlines():
            member = Path(entry)
            if not member.is_absolute():
                output_member = (output_root / member).resolve()
                archive_member = (archive.parent / member).resolve()
                member = (
                    output_member if output_member.is_file() else archive_member
                )
            else:
                member = member.resolve()
            if not member.is_file() or not _is_relative_to(
                member, output_root.resolve()
            ):
                raise RuntimeError("WebRTC thin archive has an invalid member")
            members.append(member)
        if not members:
            raise RuntimeError("WebRTC thin archive contains no object files")

        linkable_archive = archive.with_name(
            archive.stem + "_shareme" + archive.suffix
        )
        temporary_archive = linkable_archive.with_suffix(
            linkable_archive.suffix + ".tmp"
        )
        temporary_archive.unlink(missing_ok=True)
        _run(
            [
                str(llvm_ar),
                "rcsD",
                str(temporary_archive),
                *[str(member) for member in members],
            ],
            cwd=output_root,
            env=environment,
        )
        with temporary_archive.open("rb") as archive_file:
            if archive_file.read(8) != b"!<arch>\n":
                raise RuntimeError(
                    "WebRTC thin archive materialization produced an invalid archive"
                )
        temporary_archive.replace(linkable_archive)
        linkable_libraries.append(linkable_archive)

    return linkable_libraries


def _validate_build_host(system: str) -> None:
    if system != "Darwin":
        return
    try:
        result = subprocess.run(
            ["xcodebuild", "-version"],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise RuntimeError(
            "full Xcode is required to build WebRTC on macOS"
        ) from error
    if result.returncode != 0:
        raise RuntimeError("full Xcode is required to build WebRTC on macOS")


def _build(plan: Dict[str, Any]) -> None:
    depot_tools = Path(plan["depotTools"])
    source_root = Path(plan["sourceRoot"])
    output_root = Path(plan["outputRoot"])
    external_root = Path(plan["externalRoot"])
    if not depot_tools.exists() or not (source_root / ".git").exists():
        raise RuntimeError("prepare the locked WebRTC checkout before building")

    head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=source_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if head != plan["revision"]:
        raise RuntimeError("WebRTC checkout revision does not match the lock")

    system = platform.system()
    _validate_build_host(system)
    environment = _tool_environment(depot_tools)
    gn_arguments = " ".join(plan["gnArgs"])
    _run(
        ["gn", "gen", str(output_root), "--args=" + gn_arguments],
        cwd=source_root,
        env=environment,
    )
    ninja_targets = [target.rsplit(":", 1)[-1] for target in plan["targets"]]
    _run(
        ["autoninja", "-C", str(output_root), *ninja_targets],
        cwd=source_root,
        env=environment,
    )

    for header in REQUIRED_HEADERS:
        if not (source_root / header).is_file():
            raise RuntimeError("locked WebRTC checkout is missing a required header")

    include_dirs = [
        (source_root / relative_path).resolve()
        for relative_path in PUBLIC_INCLUDE_PATHS
    ]
    if any(not path.is_dir() for path in include_dirs):
        raise RuntimeError("WebRTC build is missing a required public include root")

    libraries = _library_paths(output_root, system)
    missing_libraries = [path for path in libraries if not path.is_file()]
    if missing_libraries:
        raise RuntimeError("WebRTC build did not produce all required archives")
    libraries = _materialize_linkable_libraries(
        libraries,
        source_root=source_root,
        output_root=output_root,
        system=system,
        environment=environment,
    )

    manifest = make_manifest(
        revision=plan["revision"],
        system=system,
        architecture=platform.machine(),
        include_dirs=[str(path) for path in include_dirs],
        libraries=[str(path) for path in libraries],
        compile_definitions=_compile_definitions(system),
        gn_args=plan["gnArgs"],
        msvc_runtime_library=_msvc_runtime_library(system),
    )
    manifest_path = external_root / "shareme-webrtc-manifest.json"
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        dir=external_root,
        prefix=".shareme-webrtc-manifest.",
        suffix=".tmp",
        delete=False,
    ) as temporary:
        json.dump(manifest, temporary, indent=2)
        temporary.write("\n")
        temporary_path = Path(temporary.name)
    temporary_path.replace(manifest_path)


def _parse_arguments(arguments: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare a locked external libwebrtc build for ShareMe"
    )
    parser.add_argument("--root", required=True, type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--print-plan", action="store_true")
    mode.add_argument("--prepare", action="store_true")
    mode.add_argument("--build", action="store_true")
    return parser.parse_args(arguments)


def main(arguments: Sequence[str]) -> int:
    options = _parse_arguments(arguments)
    repo_root = Path(__file__).resolve().parents[1]
    try:
        plan = create_plan(repo_root, options.root)
        if options.print_plan:
            print(json.dumps(plan, indent=2))
        elif options.prepare:
            _prepare(plan)
        else:
            _build(plan)
        return 0
    except subprocess.CalledProcessError as error:
        operation = "preparing" if options.prepare else "building"
        print(
            "WebRTC dependency "
            + operation
            + " failed: external tool exited with code "
            + str(error.returncode),
            file=sys.stderr,
        )
        return 1
    except (OSError, RuntimeError, ValueError) as error:
        operation = "planning"
        if options.prepare:
            operation = "preparing"
        elif options.build:
            operation = "building"
        print(
            "WebRTC dependency " + operation + " failed: " + str(error),
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
