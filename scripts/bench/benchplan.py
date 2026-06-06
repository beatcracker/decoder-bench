#!/usr/bin/env python3
"""Compile decoder-bench JSON definitions into suites and Ninja build files."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from benchplan_compile import (
    MEDIA_KEYS,
    VALID_CODECS,
    BenchPlanError,
    MediaSpec,
    Plan,
    build_plan,
    fail,
    load_json,
    media_from_object,
    read_presets,
    reject_unknown_keys,
)
from benchplan_ffmpeg import command_line, sample_ffmpeg_args
from benchplan_write import (
    MANIFEST_VERSION,
    helper_command,
    path_relative_to,
    print_plan,
    state_metadata_dir,
    suite_text,
    write_manifest,
    write_ninja,
    write_suite_files,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
MANAGED_ROOT = REPO_ROOT / "assets" / ".local"
MANAGED_FIXTURE_ROOT = MANAGED_ROOT / "bench"
MANAGED_STATE_ROOT = MANAGED_ROOT / "bench-state"
DEFAULT_BENCHSET = Path(__file__).resolve().with_name("benchset.json")
ATOMIC_HELPER = Path(__file__).resolve().with_name("write-output-atomically.sh")


@dataclass(frozen=True)
class BenchStatus:
    current: list[Path]
    missing: list[Path]
    dirty: list[Path]
    orphaned: list[Path]
    orphaned_state: list[Path]


def resolve_output_dirs(selector: str, out_dir: Path | None, bundled: bool) -> tuple[Path, Path]:
    if selector == "bundled" and out_dir is None:
        return MANAGED_FIXTURE_ROOT / "bundled", MANAGED_STATE_ROOT / "bundled"
    if bundled:
        if selector != "bundled":
            fail("--bundled is only allowed with the bundled selector")
        if out_dir is not None:
            fail("choose either --out or --bundled, not both")
        return MANAGED_FIXTURE_ROOT / "bundled", MANAGED_STATE_ROOT / "bundled"
    if out_dir is None:
        return MANAGED_FIXTURE_ROOT / "external", MANAGED_STATE_ROOT / "external"
    resolved_out_dir = out_dir if out_dir.is_absolute() else Path.cwd() / out_dir
    resolved_out_dir = resolved_out_dir.resolve()
    return resolved_out_dir, resolved_out_dir


def require_ninja() -> str:
    ninja = os.environ.get("NINJA")
    if ninja:
        return ninja
    ninja = shutil.which("ninja") or shutil.which("ninja-build")
    if ninja is None:
        fail("Ninja is required")
    return ninja


def run_ninja(out_dir: Path, build_file: Path, jobs: int, targets: Iterable[str] | None = None) -> None:
    ninja = require_ninja()
    build_cmd = [ninja, "-C", str(out_dir), "-f", str(build_file), "-j", str(jobs)]
    if targets is not None:
        build_cmd.extend(targets)
    subprocess.run(build_cmd, check=True)


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a positive integer") from None
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def jobs_for_cpu_count(cpu_count: int | None) -> int:
    if cpu_count is None or cpu_count < 1:
        return 1
    return max(1, (cpu_count + 2) // 3)


def default_jobs() -> int:
    return jobs_for_cpu_count(os.cpu_count())


def ffmpeg_job_count(plan: Plan) -> int:
    return len(plan.media_jobs) + len(plan.title_jobs)


def state_manifest_path(fixture_dir: Path, state_dir: Path) -> Path:
    return state_metadata_dir(fixture_dir, state_dir) / "manifest.json"


def applied_signature_root(fixture_dir: Path, state_dir: Path) -> Path:
    return state_metadata_dir(fixture_dir, state_dir) / "applied"


def applied_signature_path(fixture_dir: Path, state_dir: Path, rel_output: Path) -> Path:
    root = applied_signature_root(fixture_dir, state_dir)
    return root / rel_output.parent / f"{rel_output.name}.sig"


def read_state_json(path: Path) -> dict[str, Any] | None:
    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(data, dict):
        return None
    return data


def load_legacy_manifest_signatures(fixture_dir: Path, state_dir: Path) -> dict[Path, str]:
    data = read_state_json(state_manifest_path(fixture_dir, state_dir))
    if data is None:
        return {}
    version = data.get("version")
    if isinstance(version, int) and version >= MANIFEST_VERSION:
        return {}
    jobs = data.get("jobs")
    if not isinstance(jobs, list):
        return {}

    signatures: dict[Path, str] = {}
    for job in jobs:
        if not isinstance(job, dict):
            continue
        path = job.get("path")
        signature = job.get("signature")
        if isinstance(path, str) and isinstance(signature, str):
            signatures[Path(path)] = signature
    return signatures


def planned_media_signatures(plan: Plan) -> dict[Path, str]:
    signatures: dict[Path, str] = {}
    for filename, media in sorted(plan.media_jobs.items()):
        signatures[Path("samples") / filename] = media.signature
    for filename, title in sorted(plan.title_jobs.items()):
        signatures[Path("samples") / filename] = title.signature
    return signatures


def planned_suite_texts(plan: Plan) -> dict[Path, str]:
    return {Path("suites") / f"{suite.name}.bench": suite_text(suite) for suite in plan.suites}


def read_applied_signature(fixture_dir: Path, state_dir: Path, rel_output: Path) -> str | None:
    path = applied_signature_path(fixture_dir, state_dir, rel_output)
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    except OSError as e:
        fail(f"cannot read {path}: {e}")
    signature = text.strip()
    return signature if signature else None


def classify_suite_outputs(plan: Plan, fixture_dir: Path) -> tuple[list[Path], list[Path], list[Path]]:
    current: list[Path] = []
    missing: list[Path] = []
    dirty: list[Path] = []

    for rel_output, expected_text in sorted(planned_suite_texts(plan).items(), key=lambda item: item[0].as_posix()):
        output = fixture_dir / rel_output
        try:
            actual_text = output.read_text(encoding="utf-8")
        except FileNotFoundError:
            missing.append(rel_output)
            continue
        except OSError as e:
            fail(f"cannot read {output}: {e}")
        if actual_text == expected_text:
            current.append(rel_output)
        else:
            dirty.append(rel_output)

    return current, missing, dirty


def classify_media_outputs(
    plan: Plan,
    fixture_dir: Path,
    state_dir: Path,
    legacy_signatures: dict[Path, str] | None = None,
) -> tuple[list[Path], list[Path], list[Path], list[Path]]:
    current: list[Path] = []
    missing: list[Path] = []
    dirty: list[Path] = []
    bootstrap_current: list[Path] = []
    legacy = legacy_signatures or {}

    for rel_output, desired_signature in sorted(planned_media_signatures(plan).items(), key=lambda item: item[0].as_posix()):
        output = fixture_dir / rel_output
        if not output.exists():
            missing.append(rel_output)
            continue

        applied_signature = read_applied_signature(fixture_dir, state_dir, rel_output)
        if applied_signature == desired_signature:
            current.append(rel_output)
            continue

        legacy_signature = legacy.get(rel_output)
        if applied_signature is None and legacy_signature == desired_signature:
            current.append(rel_output)
            bootstrap_current.append(rel_output)
            continue

        dirty.append(rel_output)

    return current, missing, dirty, bootstrap_current


def write_applied_signatures(
    fixture_dir: Path,
    state_dir: Path,
    signatures: dict[Path, str],
    rel_outputs: Iterable[Path],
) -> None:
    for rel_output in rel_outputs:
        signature = signatures.get(rel_output)
        if signature is None:
            fail(f"missing applied signature for {rel_output}")
        path = applied_signature_path(fixture_dir, state_dir, rel_output)
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp_path = path.with_name(f".{path.name}.tmp")
        tmp_path.write_text(signature + "\n", encoding="utf-8")
        tmp_path.replace(path)


def rel_output_from_signature_state(rel_state: Path) -> Path:
    return rel_state.with_name(rel_state.name.removesuffix(".sig"))


def stale_applied_signature_state(plan: Plan, fixture_dir: Path, state_dir: Path) -> list[Path]:
    root = applied_signature_root(fixture_dir, state_dir)
    if not root.is_dir():
        return []

    expected = set(planned_media_signatures(plan))
    stale: list[Path] = []
    for path in root.rglob("*.sig"):
        if not path.is_file() and not path.is_symlink():
            continue
        rel_state = Path("applied") / path.relative_to(root)
        rel_output = rel_output_from_signature_state(rel_state.relative_to("applied"))
        if rel_output not in expected:
            stale.append(rel_state)

    return sorted(stale, key=lambda path: path.as_posix())


def prune_stale_applied_signature_state(plan: Plan, fixture_dir: Path, state_dir: Path, dry_run: bool) -> list[Path]:
    stale = stale_applied_signature_state(plan, fixture_dir, state_dir)
    if dry_run:
        return stale

    metadata_dir = state_metadata_dir(fixture_dir, state_dir)
    for rel_state in stale:
        path = metadata_dir / rel_state
        try:
            path.unlink()
        except FileNotFoundError:
            pass
        except OSError as e:
            fail(f"cannot remove {path}: {e}")
    return stale


def build_status(plan: Plan, fixture_dir: Path, state_dir: Path) -> BenchStatus:
    suite_current, suite_missing, suite_dirty = classify_suite_outputs(plan, fixture_dir)
    media_current, media_missing, media_dirty, _bootstrap = classify_media_outputs(
        plan,
        fixture_dir,
        state_dir,
        load_legacy_manifest_signatures(fixture_dir, state_dir),
    )
    return BenchStatus(
        current=sorted(suite_current + media_current, key=lambda path: path.as_posix()),
        missing=sorted(suite_missing + media_missing, key=lambda path: path.as_posix()),
        dirty=sorted(suite_dirty + media_dirty, key=lambda path: path.as_posix()),
        orphaned=stale_generated_assets(plan, fixture_dir),
        orphaned_state=stale_applied_signature_state(plan, fixture_dir, state_dir),
    )


def planned_fixture_targets(plan: Plan) -> set[Path]:
    suite_targets = {Path("suites") / f"{suite.name}.bench" for suite in plan.suites}
    sample_targets = {Path("samples") / filename for filename in set(plan.media_jobs) | set(plan.title_jobs)}
    return suite_targets | sample_targets


def stale_generated_assets(plan: Plan, fixture_dir: Path) -> list[Path]:
    planned = planned_fixture_targets(plan)
    sample_suffixes = {f".{codec}" for codec in VALID_CODECS}
    stale: list[Path] = []

    suites_dir = fixture_dir / "suites"
    if suites_dir.is_dir():
        for path in suites_dir.glob("*.bench"):
            if path.is_file() or path.is_symlink():
                rel = path.relative_to(fixture_dir)
                if rel not in planned:
                    stale.append(rel)

    samples_dir = fixture_dir / "samples"
    if samples_dir.is_dir():
        for path in samples_dir.iterdir():
            if path.suffix not in sample_suffixes:
                continue
            if path.is_file() or path.is_symlink():
                rel = path.relative_to(fixture_dir)
                if rel not in planned:
                    stale.append(rel)

    return sorted(stale, key=lambda path: path.as_posix())


def prune_stale_assets(plan: Plan, fixture_dir: Path, dry_run: bool) -> list[Path]:
    stale = stale_generated_assets(plan, fixture_dir)
    if dry_run:
        return stale

    for rel in stale:
        path = fixture_dir / rel
        try:
            path.unlink()
        except FileNotFoundError:
            pass
        except OSError as e:
            fail(f"cannot remove {path}: {e}")
    return stale


def print_prune_result(fixture_dir: Path, stale: list[Path], dry_run: bool, stale_state: list[Path] | None = None) -> None:
    print(f"Pruned bench content in: {fixture_dir}")
    action = "Would remove" if dry_run else "Removed"
    for rel in stale:
        print(f"{action}: {rel.as_posix()}")
    print(f"{action} stale generated assets: {len(stale)}")
    if stale_state is None:
        stale_state = []
    for rel_state in stale_state:
        print(f"{action} state: {rel_state.as_posix()}")
    if stale_state:
        print(f"{action} stale applied state: {len(stale_state)}")


def print_status_result(fixture_dir: Path, status: BenchStatus) -> None:
    print(f"Bench content status in: {fixture_dir}")
    for rel in status.missing:
        print(f"Missing: {rel.as_posix()}")
    for rel in status.dirty:
        print(f"Dirty: {rel.as_posix()}")
    for rel in status.orphaned:
        print(f"Orphaned: {rel.as_posix()}")
    for rel_state in status.orphaned_state:
        print(f"Orphaned state: {rel_state.as_posix()}")
    print(f"Current outputs: {len(status.current)}")
    print(f"Missing outputs: {len(status.missing)}")
    print(f"Dirty outputs: {len(status.dirty)}")
    print(f"Orphaned generated assets: {len(status.orphaned)}")
    print(f"Orphaned applied state: {len(status.orphaned_state)}")
    if status.missing:
        print("Bench content is not ready: required outputs are missing.")
    elif status.dirty:
        print("Warning: bench content is dirty; release-prep will stage current cached assets without regenerating them.")
    else:
        print("Bench content is current.")


def print_written_plan(plan: Plan, fixture_dir: Path, state_dir: Path) -> None:
    print(f"Planned bench content in: {fixture_dir}")
    if fixture_dir != state_dir:
        print(f"Generator state in: {state_dir}")
    print(f"Selected {sum(len(suite.tests) for suite in plan.suites)} row(s)")
    print(f"FFmpeg jobs: {ffmpeg_job_count(plan)}")


def ninja_targets_for_outputs(fixture_dir: Path, state_dir: Path, rel_outputs: Iterable[Path]) -> list[str]:
    return [path_relative_to(fixture_dir / rel_output, state_dir) for rel_output in rel_outputs]


def write_plan_artifacts(plan: Plan, selector: str, fixture_dir: Path, state_dir: Path) -> Path:
    if not ATOMIC_HELPER.is_file():
        fail(f"atomic helper is missing: {ATOMIC_HELPER}")

    fixture_dir.mkdir(parents=True, exist_ok=True)
    write_suite_files(plan, fixture_dir)
    build_file = write_ninja(plan, fixture_dir, state_dir, ATOMIC_HELPER)
    write_manifest(plan, fixture_dir, state_dir, selector)
    return build_file


def plan_command(selector: str, benchset: Path, fixture_dir: Path, state_dir: Path, print_only: bool) -> None:
    plan = build_plan(load_json(benchset), selector)
    if print_only:
        print_plan(plan, fixture_dir, selector)
        return

    legacy_signatures = load_legacy_manifest_signatures(fixture_dir, state_dir)
    desired_signatures = planned_media_signatures(plan)
    _current_media, _missing_media, _dirty_media, bootstrap_media = classify_media_outputs(
        plan,
        fixture_dir,
        state_dir,
        legacy_signatures,
    )
    write_plan_artifacts(plan, selector, fixture_dir, state_dir)
    if bootstrap_media:
        write_applied_signatures(fixture_dir, state_dir, desired_signatures, bootstrap_media)
    print_written_plan(plan, fixture_dir, state_dir)


def generate_command(
    selector: str,
    benchset: Path,
    fixture_dir: Path,
    state_dir: Path,
    jobs: int,
    prune_stale: bool,
) -> None:
    plan = build_plan(load_json(benchset), selector)
    legacy_signatures = load_legacy_manifest_signatures(fixture_dir, state_dir)
    _current_media, missing_media, dirty_media, bootstrap_media = classify_media_outputs(
        plan,
        fixture_dir,
        state_dir,
        legacy_signatures,
    )
    desired_signatures = planned_media_signatures(plan)
    build_file = write_plan_artifacts(plan, selector, fixture_dir, state_dir)
    print_written_plan(plan, fixture_dir, state_dir)
    if bootstrap_media:
        write_applied_signatures(fixture_dir, state_dir, desired_signatures, bootstrap_media)

    media_targets = sorted(missing_media + dirty_media, key=lambda path: path.as_posix())
    if media_targets:
        run_ninja(
            state_dir,
            build_file.relative_to(state_dir),
            jobs,
            ninja_targets_for_outputs(fixture_dir, state_dir, media_targets),
        )
        write_applied_signatures(fixture_dir, state_dir, desired_signatures, media_targets)
    else:
        print("FFmpeg targets already current.")

    if prune_stale:
        stale = prune_stale_assets(plan, fixture_dir, False)
        stale_state = prune_stale_applied_signature_state(plan, fixture_dir, state_dir, False)
        print_prune_result(fixture_dir, stale, False, stale_state)
    print(f"Generated bench content in: {fixture_dir}")


def prune_command(selector: str, benchset: Path, fixture_dir: Path, state_dir: Path, dry_run: bool) -> None:
    plan = build_plan(load_json(benchset), selector)
    stale = prune_stale_assets(plan, fixture_dir, dry_run)
    stale_state = prune_stale_applied_signature_state(plan, fixture_dir, state_dir, dry_run)
    print_prune_result(fixture_dir, stale, dry_run, stale_state)


def status_command(
    selector: str,
    benchset: Path,
    fixture_dir: Path,
    state_dir: Path,
    check_ready: bool,
    check_clean: bool,
) -> int:
    plan = build_plan(load_json(benchset), selector)
    status = build_status(plan, fixture_dir, state_dir)
    print_status_result(fixture_dir, status)
    if check_clean and (status.missing or status.dirty):
        return 1
    if check_ready and status.missing:
        return 1
    return 0


def media_from_args(args: argparse.Namespace, presets: dict[str, Any]) -> MediaSpec:
    values: dict[str, Any] = {
        "codec": args.codec,
        "w": args.w,
        "h": args.h,
        "fps": args.fps,
        "kbps": args.kbps,
        "preset": args.preset,
        "sample_s": args.sample_s,
    }
    if args.peak_kbps is not None:
        values["peak_kbps"] = args.peak_kbps
    reject_unknown_keys(values, MEDIA_KEYS, "render")
    return media_from_object(values, presets, "render")


def run_with_atomic_helper(output: Path, signature: str, argv: list[str]) -> None:
    if not ATOMIC_HELPER.is_file():
        fail(f"atomic helper is missing: {ATOMIC_HELPER}")
    subprocess.run(helper_command(ATOMIC_HELPER, Path.cwd(), str(output), signature, argv), check=True)


def render_command(out_dir: Path, media: MediaSpec, presets: dict[str, Any], print_only: bool) -> None:
    resolved_out_dir = out_dir if out_dir.is_absolute() else Path.cwd() / out_dir
    output = resolved_out_dir.resolve() / media.filename
    argv = sample_ffmpeg_args(media, str(output), presets)
    if print_only:
        print(f"Output: {output}")
        print(command_line(argv))
        return

    run_with_atomic_helper(output, media.signature, argv[:-1])
    print(f"Rendered sample: {output}")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan_parser = subparsers.add_parser("plan", help="validate benchset data and write suite/Ninja plan files")
    plan_parser.add_argument("selector")
    plan_parser.add_argument("--benchset", type=Path, default=DEFAULT_BENCHSET)
    plan_parser.add_argument("--out", type=Path)
    plan_parser.add_argument("--bundled", action="store_true")
    plan_parser.add_argument("--print", action="store_true", dest="print_only")

    generate_parser = subparsers.add_parser("generate", help="write suite/Ninja plan files and run FFmpeg jobs")
    generate_parser.add_argument("selector")
    generate_parser.add_argument("--benchset", type=Path, default=DEFAULT_BENCHSET)
    generate_parser.add_argument("--out", type=Path)
    generate_parser.add_argument("--bundled", action="store_true")
    generate_parser.add_argument("--jobs", type=positive_int, default=default_jobs(), help="parallel Ninja jobs")
    generate_parser.add_argument("--prune-stale", action="store_true", help="remove generated assets outside this plan")

    prune_parser = subparsers.add_parser("prune", help="remove generated assets outside the selected bench plan")
    prune_parser.add_argument("selector")
    prune_parser.add_argument("--benchset", type=Path, default=DEFAULT_BENCHSET)
    prune_parser.add_argument("--out", type=Path)
    prune_parser.add_argument("--bundled", action="store_true")
    prune_parser.add_argument("--dry-run", action="store_true")

    status_parser = subparsers.add_parser("status", help="report current, missing, dirty, and orphaned bench content")
    status_parser.add_argument("selector")
    status_parser.add_argument("--benchset", type=Path, default=DEFAULT_BENCHSET)
    status_parser.add_argument("--out", type=Path)
    status_parser.add_argument("--bundled", action="store_true")
    status_parser.add_argument("--check-ready", action="store_true", help="fail if required outputs are missing")
    status_parser.add_argument("--check-clean", action="store_true", help="fail if required outputs are missing or dirty")

    render_parser = subparsers.add_parser("render", help="render one sample directly with FFmpeg")
    render_parser.add_argument("out_dir", type=Path)
    render_parser.add_argument("--benchset", type=Path, default=DEFAULT_BENCHSET)
    render_parser.add_argument("--codec", required=True)
    render_parser.add_argument("--w", type=positive_int, required=True)
    render_parser.add_argument("--h", type=positive_int, required=True)
    render_parser.add_argument("--fps", type=positive_int, required=True)
    render_parser.add_argument("--kbps", type=positive_int, required=True)
    render_parser.add_argument("--peak-kbps", type=positive_int)
    render_parser.add_argument("--preset", default="motion-basic")
    render_parser.add_argument("--sample-s", type=positive_int, default=3)
    render_parser.add_argument("--print", action="store_true", dest="print_only")

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "plan":
            fixture_dir, state_dir = resolve_output_dirs(args.selector, args.out, args.bundled)
            plan_command(args.selector, args.benchset, fixture_dir, state_dir, args.print_only)
        elif args.command == "generate":
            fixture_dir, state_dir = resolve_output_dirs(args.selector, args.out, args.bundled)
            generate_command(args.selector, args.benchset, fixture_dir, state_dir, args.jobs, args.prune_stale)
        elif args.command == "prune":
            fixture_dir, state_dir = resolve_output_dirs(args.selector, args.out, args.bundled)
            prune_command(args.selector, args.benchset, fixture_dir, state_dir, args.dry_run)
        elif args.command == "status":
            fixture_dir, state_dir = resolve_output_dirs(args.selector, args.out, args.bundled)
            return status_command(
                args.selector,
                args.benchset,
                fixture_dir,
                state_dir,
                args.check_ready,
                args.check_clean,
            )
        elif args.command == "render":
            presets = read_presets(load_json(args.benchset))
            render_command(args.out_dir, media_from_args(args, presets), presets, args.print_only)
        else:
            parser.error(f"unknown command {args.command}")
    except BenchPlanError as e:
        print(f"benchplan: {e}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as e:
        return e.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
