"""Write generated bench suites, Ninja files, and manifests."""

from __future__ import annotations

import json
import os
from pathlib import Path

from benchplan_compile import GENERATOR_VERSION, Plan, SuitePlan, fail
from benchplan_ffmpeg import command_line, sample_ffmpeg_args, title_ffmpeg_args


MANIFEST_VERSION = 2


def suite_text(suite: SuitePlan) -> str:
    lines: list[str] = []
    if suite.source_buffer_mib is not None:
        lines += ["[suite]", f"source_buffer_mib = {suite.source_buffer_mib}", ""]

    if suite.title is not None:
        title = suite.title
        lines += [
            "[title-card]",
            f"file = {title.filename}",
            f"fps = {title.fps}",
            f"run_seconds = {title.run_s}",
            "skip_stats = true",
            "",
        ]

    for index, test in enumerate(suite.tests):
        media = test.media
        if index > 0:
            lines.append("")
        lines += [
            f"[{media.filename}]",
            f"file = {media.filename}",
            f"fps = {media.fps}",
            f"run_seconds = {test.run_s}",
        ]

    return "\n".join(lines) + "\n"


def write_suite_files(plan: Plan, out_dir: Path) -> None:
    suites_dir = out_dir / "suites"
    suites_dir.mkdir(parents=True, exist_ok=True)

    for suite in plan.suites:
        (suites_dir / f"{suite.name}.bench").write_text(suite_text(suite), encoding="utf-8")


def ninja_escape_path(value: str) -> str:
    out = []
    for ch in value.replace("\\", "/"):
        if ch == " ":
            out.append("$ ")
        elif ch == ":":
            out.append("$:")
        elif ch == "$":
            out.append("$$")
        elif ch == "#":
            out.append("$#")
        elif ch in "\r\n":
            fail("ninja paths must be single-line")
        else:
            out.append(ch)
    return "".join(out)


def ninja_escape_value(value: str) -> str:
    if "\n" in value or "\r" in value:
        fail("ninja command values must be single-line")
    return value.replace("$", "$$")


def path_relative_to(path: Path, base: Path) -> str:
    try:
        return os.path.relpath(path.resolve(), base.resolve()).replace(os.sep, "/")
    except ValueError:
        return path.resolve().as_posix()


def state_metadata_dir(fixture_dir: Path, state_dir: Path) -> Path:
    if fixture_dir == state_dir:
        return state_dir / ".benchgen"
    return state_dir


def command_signature(signature: str) -> str:
    return f"v{GENERATOR_VERSION}|{signature}"


def helper_command(helper: Path, work_dir: Path, output: str, signature: str, argv: list[str]) -> list[str]:
    return [
        "bash",
        path_relative_to(helper, work_dir),
        output,
        command_signature(signature),
        "--",
        *argv,
    ]


def write_ninja(plan: Plan, fixture_dir: Path, state_dir: Path, helper: Path) -> Path:
    metadata_dir = state_metadata_dir(fixture_dir, state_dir)
    samples_dir = fixture_dir / "samples"
    metadata_dir.mkdir(parents=True, exist_ok=True)
    samples_dir.mkdir(parents=True, exist_ok=True)

    helper_input = ninja_escape_path(path_relative_to(helper, state_dir))
    implicit_inputs = f" | {helper_input}"
    outputs: list[str] = []
    lines = [
        "ninja_required_version = 1.3",
        "",
        "rule ffmpeg",
        "  command = $cmd",
        "  description = FFmpeg $out",
        "  restat = 1",
        "",
    ]

    for filename, media in sorted(plan.media_jobs.items()):
        output = path_relative_to(fixture_dir / "samples" / filename, state_dir)
        outputs.append(output)
        argv = sample_ffmpeg_args(media, output, plan.presets)
        lines += [
            f"build {ninja_escape_path(output)}: ffmpeg{implicit_inputs}",
            f"  cmd = {ninja_escape_value(command_line(helper_command(helper, state_dir, output, media.signature, argv[:-1])))}",
            "",
        ]

    for filename, title in sorted(plan.title_jobs.items()):
        output = path_relative_to(fixture_dir / "samples" / filename, state_dir)
        outputs.append(output)
        argv = title_ffmpeg_args(title, output)
        lines += [
            f"build {ninja_escape_path(output)}: ffmpeg{implicit_inputs}",
            f"  cmd = {ninja_escape_value(command_line(helper_command(helper, state_dir, output, title.signature, argv[:-1])))}",
            "",
        ]

    outputs_text = " ".join(ninja_escape_path(output) for output in outputs)
    lines.append(f"build all: phony {outputs_text}".rstrip())
    lines.append("default all")
    lines.append("")
    build_file = metadata_dir / "build.ninja"
    build_file.write_text("\n".join(lines), encoding="utf-8")
    return build_file


def write_manifest(plan: Plan, fixture_dir: Path, state_dir: Path, selector: str) -> None:
    jobs = []
    for filename, media in sorted(plan.media_jobs.items()):
        jobs.append({"kind": "sample", "path": f"samples/{filename}", "signature": media.signature})
    for filename, title in sorted(plan.title_jobs.items()):
        jobs.append({"kind": "title", "path": f"samples/{filename}", "signature": title.signature})

    manifest = {
        "version": MANIFEST_VERSION,
        "selector": selector,
        "suites": [suite.name for suite in plan.suites],
        "jobs": jobs,
    }
    path = state_metadata_dir(fixture_dir, state_dir) / "manifest.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def print_plan(plan: Plan, out_dir: Path, selector: str) -> None:
    print(f"Plan: selector={selector} out={out_dir}")
    for suite in plan.suites:
        print(f"suite={suite.name} group={suite.group} tests={len(suite.tests)}")
        if suite.title is not None:
            title = suite.title
            print(
                f"title={title.filename} codec={title.codec} {title.w}x{title.h} "
                f"fps={title.fps} kbps={title.kbps}"
            )
        for test in suite.tests:
            media = test.media
            peak = media.peak_kbps if media.peak_kbps is not None else "-"
            print(
                f"sample={media.filename} codec={media.codec} {media.w}x{media.h} "
                f"fps={media.fps} kbps={media.kbps} peak={peak} "
                f"preset={media.preset} sample_s={media.sample_s} run_s={test.run_s}"
            )
    print(f"Plan selected {sum(len(suite.tests) for suite in plan.suites)} row(s)")
