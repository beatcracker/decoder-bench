"""Compile benchset JSON into in-memory bench plans."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


VALID_CODECS = {"h264", "hevc"}
TEXT_SAFE_RE = re.compile(r"^[A-Za-z0-9._-]+$")
SOURCE_BUFFER_MIN_MIB = 32
SOURCE_BUFFER_MAX_MIB = 512
GENERATOR_VERSION = 1

BUILTIN_PRESETS = {
    "motion-basic": "testsrc2=s={w}x{h}:rate={fps}:duration={sample_s},scroll=horizontal=0.003:vertical=0.001",
    "motion-detail": (
        "testsrc2=s={w}x{h}:rate={fps}:duration={sample_s}[base];"
        "mandelbrot=s={w}x{h}:r={fps}[fx];"
        "[base][fx]blend=all_mode=overlay:all_opacity=0.35,hue=H=t*90"
    ),
    "motion-noise": (
        "testsrc2=s={w}x{h}:rate={fps}:duration={sample_s},"
        "noise=all_strength=10:all_flags=t+u,hue=H=t*180"
    ),
}

COMMENT_KEYS = {"_"}
STREAM_KEYS = {"codec", "w", "h", "fps", "kbps"} | COMMENT_KEYS
MEDIA_KEYS = STREAM_KEYS | {"peak_kbps", "preset", "sample_s", "_"}
TEST_KEYS = MEDIA_KEYS | {"run_s"}
TITLE_KEYS = STREAM_KEYS | {"title", "description", "run_s"}
SUITE_KEYS = {
    "name",
    "group",
    "source_buffer_mib",
    "title",
    "tests",
} | COMMENT_KEYS


class BenchPlanError(Exception):
    pass


@dataclass(frozen=True)
class MediaSpec:
    codec: str
    w: int
    h: int
    fps: int
    kbps: int
    preset: str
    sample_s: int
    peak_kbps: int | None = None

    @property
    def stem(self) -> str:
        peak = f"_peak{self.peak_kbps}kbps" if self.peak_kbps is not None else ""
        return (
            f"{self.codec}_{self.w}x{self.h}_{self.fps}fps_"
            f"{self.kbps}kbps_{self.preset}{peak}_dur{self.sample_s}s"
        )

    @property
    def filename(self) -> str:
        return f"{self.stem}.{self.codec}"

    @property
    def signature(self) -> str:
        peak = "" if self.peak_kbps is None else str(self.peak_kbps)
        return "|".join(
            [
                "sample",
                self.codec,
                str(self.w),
                str(self.h),
                str(self.fps),
                str(self.kbps),
                peak,
                self.preset,
                str(self.sample_s),
            ]
        )


@dataclass(frozen=True)
class SuiteTest:
    media: MediaSpec
    run_s: int


@dataclass(frozen=True)
class TitleSpec:
    suite: str
    title: str
    description: str
    test_count: int
    runtime_s: int
    run_s: int
    codec: str
    w: int
    h: int
    fps: int
    kbps: int

    @property
    def stem(self) -> str:
        return (
            f"title_{sanitize_name(self.suite)}_{self.codec}_{self.w}x{self.h}_"
            f"{self.fps}fps_{self.kbps}kbps_{self.test_count}tests_"
            f"{self.runtime_s}s_card{self.run_s}s"
        )

    @property
    def filename(self) -> str:
        return f"{self.stem}.{self.codec}"

    @property
    def signature(self) -> str:
        return "|".join(
            [
                "title",
                self.suite,
                self.title,
                self.description,
                str(self.test_count),
                str(self.runtime_s),
                self.codec,
                str(self.w),
                str(self.h),
                str(self.fps),
                str(self.kbps),
                str(self.run_s),
                str(GENERATOR_VERSION),
            ]
        )


@dataclass
class SuitePlan:
    name: str
    group: str
    source_buffer_mib: int | None
    tests: list[SuiteTest]
    title: TitleSpec | None


@dataclass
class Plan:
    suites: list[SuitePlan]
    media_jobs: dict[str, MediaSpec]
    title_jobs: dict[str, TitleSpec]
    presets: dict[str, Any]


def fail(message: str) -> None:
    raise BenchPlanError(message)


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        fail(f"cannot read {path}: {e}")
    except json.JSONDecodeError as e:
        fail(f"{path}:{e.lineno}:{e.colno}: {e.msg}")
    if not isinstance(data, dict):
        fail("benchset root must be an object")
    return data


def require_object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{path} must be an object")
    return value


def require_list(value: Any, path: str) -> list[Any]:
    if not isinstance(value, list):
        fail(f"{path} must be a list")
    return value


def require_string(value: Any, path: str) -> str:
    if not isinstance(value, str) or value == "":
        fail(f"{path} must be a non-empty string")
    if "\n" in value or "\r" in value:
        fail(f"{path} must be a single-line string")
    return value


def require_int(value: Any, path: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(f"{path} must be a positive integer")
    return value


def require_name(value: Any, path: str) -> str:
    name = require_string(value, path)
    if not TEXT_SAFE_RE.match(name):
        fail(f"{path} may only contain letters, digits, '.', '_', and '-'")
    return name


def reject_unknown_keys(obj: dict[str, Any], allowed: set[str], path: str) -> None:
    for key in obj:
        if key not in allowed:
            fail(f"{path}.{key} is not a supported key")


def text(obj: dict[str, Any], key: str, path: str, default: str | None = None) -> str:
    if key not in obj:
        if default is not None:
            return default
        fail(f"{path}.{key} is required")
    return require_string(obj[key], f"{path}.{key}")


def pos_int(obj: dict[str, Any], key: str, path: str) -> int:
    if key not in obj:
        fail(f"{path}.{key} is required")
    return require_int(obj[key], f"{path}.{key}")


def name_value(obj: dict[str, Any], key: str, path: str) -> str:
    if key not in obj:
        fail(f"{path}.{key} is required")
    return require_name(obj[key], f"{path}.{key}")


def sanitize_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]", "_", value)


def known_codec(value: Any, path: str) -> str:
    codec = require_string(value, path).lower()
    if codec not in VALID_CODECS:
        fail(f"{path} must be one of: {', '.join(sorted(VALID_CODECS))}")
    return codec


def read_presets(data: dict[str, Any]) -> dict[str, Any]:
    value = data.get("presets", BUILTIN_PRESETS)
    return require_object(value, "presets")


def preset_template(presets: dict[str, Any], preset: str, path: str) -> str:
    if preset not in presets:
        fail(f"{path} must be one of: {', '.join(sorted(presets))}")
    return require_string(presets[preset], f"presets.{preset}")


def known_preset(value: Any, presets: dict[str, Any], path: str) -> str:
    preset = require_name(value, path)
    preset_template(presets, preset, path)
    return preset


def read_source_buffer_mib(value: Any, path: str) -> int:
    mib = require_int(value, path)
    if mib < SOURCE_BUFFER_MIN_MIB or mib > SOURCE_BUFFER_MAX_MIB:
        fail(f"{path} must be in the range {SOURCE_BUFFER_MIN_MIB}..{SOURCE_BUFFER_MAX_MIB}")
    return mib


def media_from_object(obj: dict[str, Any], presets: dict[str, Any], path: str) -> MediaSpec:
    peak_kbps = require_int(obj["peak_kbps"], f"{path}.peak_kbps") if "peak_kbps" in obj else None
    media = MediaSpec(
        codec=known_codec(text(obj, "codec", path), f"{path}.codec"),
        w=pos_int(obj, "w", path),
        h=pos_int(obj, "h", path),
        fps=pos_int(obj, "fps", path),
        kbps=pos_int(obj, "kbps", path),
        preset=known_preset(text(obj, "preset", path), presets, f"{path}.preset"),
        sample_s=pos_int(obj, "sample_s", path),
        peak_kbps=peak_kbps,
    )
    if media.peak_kbps is not None and media.peak_kbps < media.kbps:
        fail(f"{path}.peak_kbps must be greater than or equal to {path}.kbps")
    return media


def compile_test(value: Any, presets: dict[str, Any], path: str) -> SuiteTest:
    test = require_object(value, path)
    reject_unknown_keys(test, TEST_KEYS, path)

    media = media_from_object(test, presets, path)
    run_s = pos_int(test, "run_s", path)
    if media.sample_s < run_s:
        fail(f"{path}.sample_s must be greater than or equal to {path}.run_s")
    return SuiteTest(media=media, run_s=run_s)


def compile_title(
    value: Any,
    path: str,
    suite: str,
    tests: list[SuiteTest],
) -> TitleSpec:
    obj = require_object(value, path)
    reject_unknown_keys(obj, TITLE_KEYS, path)
    return TitleSpec(
        suite=suite,
        title=text(obj, "title", path),
        description=text(obj, "description", path),
        test_count=len(tests),
        runtime_s=sum(test.run_s for test in tests),
        run_s=pos_int(obj, "run_s", path),
        codec=known_codec(text(obj, "codec", path), f"{path}.codec"),
        w=pos_int(obj, "w", path),
        h=pos_int(obj, "h", path),
        fps=pos_int(obj, "fps", path),
        kbps=pos_int(obj, "kbps", path),
    )


def compile_suite(suite_obj: dict[str, Any], presets: dict[str, Any], path: str) -> SuitePlan:
    reject_unknown_keys(suite_obj, SUITE_KEYS, path)
    name = name_value(suite_obj, "name", path)
    group = text(suite_obj, "group", path)

    source_buffer_mib = None
    if "source_buffer_mib" in suite_obj:
        source_buffer_mib = read_source_buffer_mib(
            suite_obj["source_buffer_mib"],
            f"{path}.source_buffer_mib",
        )

    if "tests" not in suite_obj:
        fail(f"{path}.tests is required")
    tests_data = require_list(suite_obj["tests"], f"{path}.tests")
    if not tests_data:
        fail(f"{path}.tests must not be empty")

    tests: list[SuiteTest] = []
    seen_tests: set[str] = set()
    for index, value in enumerate(tests_data):
        test_path = f"{path}.tests[{index}]"
        test = compile_test(value, presets, test_path)
        if test.media.filename in seen_tests:
            fail(f"{test_path} duplicates test fixture '{test.media.filename}'")
        seen_tests.add(test.media.filename)
        tests.append(test)

    title = None
    if "title" in suite_obj:
        title = compile_title(
            suite_obj["title"],
            f"{path}.title",
            name,
            tests,
        )

    return SuitePlan(
        name=name,
        group=group,
        source_buffer_mib=source_buffer_mib,
        tests=tests,
        title=title,
    )


def selected_suite_values(data: dict[str, Any], selector: str) -> list[tuple[dict[str, Any], str]]:
    suites_data = require_list(data.get("suites", []), "suites")
    selected: list[tuple[dict[str, Any], str]] = []

    for index, value in enumerate(suites_data):
        path = f"suites[{index}]"
        if selector == "all":
            selected.append((require_object(value, path), path))
            continue
        if isinstance(value, dict) and value.get("group") == selector:
            selected.append((value, path))

    if not selected:
        fail(f"unknown selector '{selector}'")
    return selected


def register_target(targets: dict[str, str], path: str, signature: str) -> None:
    existing = targets.get(path)
    if existing is not None and existing != signature:
        fail(f"generated target '{path}' has conflicting signatures")
    targets[path] = signature


def build_plan(data: dict[str, Any], selector: str) -> Plan:
    presets = read_presets(data)
    suites = []
    seen_suites: set[str] = set()
    for suite_obj, path in selected_suite_values(data, selector):
        suite = compile_suite(suite_obj, presets, path)
        if suite.name in seen_suites:
            fail(f"{path}.name duplicates suite '{suite.name}'")
        seen_suites.add(suite.name)
        suites.append(suite)

    targets: dict[str, str] = {}
    media_jobs: dict[str, MediaSpec] = {}
    title_jobs: dict[str, TitleSpec] = {}

    for suite in suites:
        if suite.title is not None:
            path = f"samples/{suite.title.filename}"
            register_target(targets, path, suite.title.signature)
            title_jobs[suite.title.filename] = suite.title

        for test in suite.tests:
            media = test.media
            path = f"samples/{media.filename}"
            register_target(targets, path, media.signature)
            existing = media_jobs.get(media.filename)
            if existing is not None and existing.signature != media.signature:
                fail(f"sample file '{media.filename}' has conflicting generation parameters")
            media_jobs[media.filename] = media

    return Plan(suites=suites, media_jobs=media_jobs, title_jobs=title_jobs, presets=presets)
