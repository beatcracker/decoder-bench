#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import benchplan  # noqa: E402


def minimal_data() -> dict:
    return {
        "suites": [
            {
                "name": "quick-smoke",
                "group": "bundled",
                "title": {
                    "title": "Quick Smoke",
                    "description": "Smoke suite.",
                    "codec": "hevc",
                    "w": 1920,
                    "h": 1080,
                    "fps": 60,
                    "kbps": 30000,
                    "run_s": 3,
                },
                "tests": [
                    {
                        "codec": "hevc",
                        "w": 1920,
                        "h": 1080,
                        "fps": 60,
                        "kbps": 30000,
                        "preset": "motion-basic",
                        "sample_s": 10,
                        "run_s": 5,
                    },
                    {
                        "codec": "hevc",
                        "w": 3840,
                        "h": 2160,
                        "fps": 60,
                        "kbps": 50000,
                        "preset": "motion-basic",
                        "sample_s": 10,
                        "run_s": 5,
                    },
                ],
            }
        ]
    }


class BenchPlanTests(unittest.TestCase):
    def test_default_output_dirs_use_managed_external_cache(self) -> None:
        fixture_dir, state_dir = benchplan.resolve_output_dirs("external", None, False)

        self.assertEqual(fixture_dir, benchplan.MANAGED_FIXTURE_ROOT / "external")
        self.assertEqual(state_dir, benchplan.MANAGED_STATE_ROOT / "external")

    def test_bundled_output_dirs_use_managed_bundled_cache(self) -> None:
        fixture_dir, state_dir = benchplan.resolve_output_dirs("bundled", None, True)

        self.assertEqual(fixture_dir, benchplan.MANAGED_FIXTURE_ROOT / "bundled")
        self.assertEqual(state_dir, benchplan.MANAGED_STATE_ROOT / "bundled")

    def test_bundled_selector_auto_uses_managed_bundled_cache(self) -> None:
        fixture_dir, state_dir = benchplan.resolve_output_dirs("bundled", None, False)

        self.assertEqual(fixture_dir, benchplan.MANAGED_FIXTURE_ROOT / "bundled")
        self.assertEqual(state_dir, benchplan.MANAGED_STATE_ROOT / "bundled")

    def test_custom_output_dirs_are_self_contained(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "custom-cache"
            fixture_dir, state_dir = benchplan.resolve_output_dirs("external", out, False)

            self.assertEqual(fixture_dir, out.resolve())
            self.assertEqual(state_dir, out.resolve())

    def test_bundled_selector_with_custom_output_dir_is_self_contained(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "bundled-cache"
            fixture_dir, state_dir = benchplan.resolve_output_dirs("bundled", out, False)

            self.assertEqual(fixture_dir, out.resolve())
            self.assertEqual(state_dir, out.resolve())

    def test_media_filename_matches_stable_shape(self) -> None:
        media = benchplan.MediaSpec(
            codec="hevc",
            w=3840,
            h=2160,
            fps=60,
            kbps=60000,
            peak_kbps=180000,
            preset="motion-noise",
            sample_s=30,
        )

        self.assertEqual(
            media.filename,
            "hevc_3840x2160_60fps_60000kbps_motion-noise_peak180000kbps_dur30s.hevc",
        )

    def test_ignored_comment_does_not_change_filename_label(self) -> None:
        data = minimal_data()
        data["suites"][0]["tests"][0]["_"] = "baseline"

        plan = benchplan.build_plan(data, "bundled")
        media = plan.suites[0].tests[0].media

        self.assertEqual(media.filename, "hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchplan.write_suite_files(plan, out)

            suite_text = (out / "suites" / "quick-smoke.bench").read_text(encoding="utf-8")
            self.assertIn("[hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc]", suite_text)
            self.assertIn("file = hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc", suite_text)

    def test_default_jobs_scales_from_logical_cpus(self) -> None:
        self.assertEqual(benchplan.jobs_for_cpu_count(12), 4)
        self.assertEqual(benchplan.jobs_for_cpu_count(6), 2)
        self.assertEqual(benchplan.jobs_for_cpu_count(4), 2)
        self.assertEqual(benchplan.jobs_for_cpu_count(None), 1)

    def test_self_contained_rows_and_title(self) -> None:
        plan = benchplan.build_plan(minimal_data(), "bundled")

        self.assertEqual(len(plan.suites), 1)
        self.assertEqual(len(plan.media_jobs), 2)
        self.assertIn("hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc", plan.media_jobs)
        self.assertEqual(len(plan.title_jobs), 1)
        suite = plan.suites[0]
        self.assertIsNotNone(suite.title)
        self.assertEqual(suite.tests[0].run_s, 5)

    def test_suite_defaults_are_not_supported(self) -> None:
        data = minimal_data()
        data["suites"][0]["defaults"] = {"codec": "hevc"}

        with self.assertRaisesRegex(benchplan.BenchPlanError, "defaults.*supported key"):
            benchplan.build_plan(data, "all")

    def test_title_is_optional(self) -> None:
        data = minimal_data()
        del data["suites"][0]["title"]

        plan = benchplan.build_plan(data, "bundled")

        self.assertEqual(len(plan.title_jobs), 0)
        self.assertIsNone(plan.suites[0].title)

    def test_unknown_group_fails(self) -> None:
        with self.assertRaisesRegex(benchplan.BenchPlanError, "unknown selector"):
            benchplan.build_plan(minimal_data(), "missing")

    def test_missing_description_is_allowed_without_title(self) -> None:
        data = minimal_data()
        del data["suites"][0]["title"]

        plan = benchplan.build_plan(data, "all")

        self.assertIsNone(plan.suites[0].title)

    def test_missing_description_fails_when_title_exists(self) -> None:
        data = minimal_data()
        del data["suites"][0]["title"]["description"]

        with self.assertRaisesRegex(benchplan.BenchPlanError, "description is required"):
            benchplan.build_plan(data, "all")

    def test_missing_title_text_fails_when_title_exists(self) -> None:
        data = minimal_data()
        del data["suites"][0]["title"]["title"]

        with self.assertRaisesRegex(benchplan.BenchPlanError, "title.title is required"):
            benchplan.build_plan(data, "all")

    def test_unselected_invalid_suite_does_not_block_selected_group(self) -> None:
        data = minimal_data()
        data["suites"].append(
            {
                "name": "broken-later",
                "group": "later",
                "unexpected": "ignored until selected",
                "tests": [{"preset": "missing"}],
            }
        )

        plan = benchplan.build_plan(data, "bundled")

        self.assertEqual([suite.name for suite in plan.suites], ["quick-smoke"])

    def test_peak_kbps_must_not_be_lower_than_kbps(self) -> None:
        data = minimal_data()
        data["suites"][0]["tests"] = [
            {
                "codec": "hevc",
                "w": 1920,
                "h": 1080,
                "fps": 60,
                "kbps": 30000,
                "peak_kbps": 10000,
                "preset": "motion-basic",
                "sample_s": 10,
                "run_s": 5,
            }
        ]

        with self.assertRaisesRegex(benchplan.BenchPlanError, "peak_kbps"):
            benchplan.build_plan(data, "all")

    def test_sample_length_must_cover_run_length(self) -> None:
        data = minimal_data()
        data["suites"][0]["tests"] = [
            {
                "codec": "hevc",
                "w": 1920,
                "h": 1080,
                "fps": 60,
                "kbps": 30000,
                "preset": "motion-basic",
                "sample_s": 5,
                "run_s": 10,
            }
        ]

        with self.assertRaisesRegex(benchplan.BenchPlanError, "sample_s"):
            benchplan.build_plan(data, "all")

    def test_unknown_preset_in_selected_test_fails(self) -> None:
        data = minimal_data()
        data["suites"][0]["tests"][0]["preset"] = "weird"

        with self.assertRaisesRegex(benchplan.BenchPlanError, "tests\\[0\\].preset"):
            benchplan.build_plan(data, "all")

    def test_conflicting_duplicate_fixture_fails(self) -> None:
        data = minimal_data()
        data["suites"][0]["tests"] = [
            {
                "_": "same",
                "codec": "hevc",
                "w": 1920,
                "h": 1080,
                "fps": 60,
                "kbps": 30000,
                "preset": "motion-basic",
                "sample_s": 10,
                "run_s": 5,
            },
            {
                "_": "same",
                "codec": "hevc",
                "w": 1920,
                "h": 1080,
                "fps": 60,
                "kbps": 30000,
                "preset": "motion-basic",
                "sample_s": 10,
                "run_s": 5,
            },
        ]

        with self.assertRaisesRegex(benchplan.BenchPlanError, "duplicates test fixture"):
            benchplan.build_plan(data, "all")

    def test_same_comment_across_suites_keeps_distinct_param_filenames(self) -> None:
        data = minimal_data()
        data["suites"][0]["tests"] = [
            {
                "_": "shared",
                "codec": "hevc",
                "w": 1920,
                "h": 1080,
                "fps": 60,
                "kbps": 30000,
                "preset": "motion-basic",
                "sample_s": 10,
                "run_s": 5,
            }
        ]

        other = copy.deepcopy(data["suites"][0])
        other["name"] = "other-smoke"
        other["tests"] = [
            {
                "_": "shared",
                "codec": "hevc",
                "w": 1920,
                "h": 1080,
                "fps": 60,
                "kbps": 40000,
                "preset": "motion-basic",
                "sample_s": 10,
                "run_s": 5,
            }
        ]
        data["suites"].append(other)

        plan = benchplan.build_plan(data, "all")

        self.assertIn("hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc", plan.media_jobs)
        self.assertIn("hevc_1920x1080_60fps_40000kbps_motion-basic_dur10s.hevc", plan.media_jobs)
        self.assertEqual(len(plan.media_jobs), 2)

    def test_suite_source_buffer_mib_is_written(self) -> None:
        data = minimal_data()
        data["suites"][0]["source_buffer_mib"] = 128

        plan = benchplan.build_plan(data, "bundled")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchplan.write_suite_files(plan, out)

            suite_text = (out / "suites" / "quick-smoke.bench").read_text(encoding="utf-8")
            self.assertIn("[suite]", suite_text)
            self.assertIn("source_buffer_mib = 128", suite_text)

    def test_suite_source_buffer_mib_must_be_in_range(self) -> None:
        data = minimal_data()
        data["suites"][0]["source_buffer_mib"] = 16

        with self.assertRaisesRegex(benchplan.BenchPlanError, "source_buffer_mib"):
            benchplan.build_plan(data, "all")

    def test_write_suite_and_ninja(self) -> None:
        plan = benchplan.build_plan(minimal_data(), "bundled")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(minimal_data()), encoding="utf-8")
            benchplan.write_suite_files(plan, out)
            build_file = benchplan.write_ninja(plan, out, out, benchplan.ATOMIC_HELPER)

            suite_text = (out / "suites" / "quick-smoke.bench").read_text(encoding="utf-8")
            self.assertIn("[title-card]", suite_text)
            self.assertIn("skip_stats = true", suite_text)
            self.assertIn("[hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc]", suite_text)
            self.assertIn("run_seconds = 5", suite_text)
            self.assertIn("run_seconds = 3", suite_text)

            self.assertEqual(build_file, out / ".benchgen" / "build.ninja")
            ninja_text = (out / ".benchgen" / "build.ninja").read_text(encoding="utf-8")
            self.assertIn("rule ffmpeg", ninja_text)
            self.assertIn("write-output-atomically", ninja_text)
            self.assertIn("libx265", ninja_text)
            self.assertIn("testsrc2", ninja_text)
            self.assertIn("title_quick-smoke_hevc_1920x1080_60fps_30000kbps_2tests_10s_card3s.hevc", ninja_text)

    def test_managed_layout_writes_state_outside_fixture_root(self) -> None:
        plan = benchplan.build_plan(minimal_data(), "bundled")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture_dir = root / "bench" / "external"
            state_dir = root / "bench-state" / "external"
            benchset = root / "benchset.json"
            benchset.write_text(json.dumps(minimal_data()), encoding="utf-8")

            benchplan.write_suite_files(plan, fixture_dir)
            build_file = benchplan.write_ninja(plan, fixture_dir, state_dir, benchplan.ATOMIC_HELPER)

            self.assertFalse((fixture_dir / ".benchgen").exists())
            self.assertEqual(build_file, state_dir / "build.ninja")
            ninja_text = (state_dir / "build.ninja").read_text(encoding="utf-8")
            self.assertIn("bench/external/samples", ninja_text)
            self.assertIn("write-output-atomically", ninja_text)

    def test_benchset_presets_drive_generated_ffmpeg_commands(self) -> None:
        data = minimal_data()
        data["presets"] = {
            "motion-basic": "testsrc2=s={w}x{h}:rate={fps}:duration={sample_s}",
        }
        plan = benchplan.build_plan(data, "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")
            benchplan.write_ninja(plan, out, out, benchplan.ATOMIC_HELPER)

            ninja_text = (out / ".benchgen" / "build.ninja").read_text(encoding="utf-8")
            self.assertIn("testsrc2", ninja_text)
            self.assertNotIn("mandelbrot", ninja_text)

    def test_malformed_preset_template_reports_benchplan_error(self) -> None:
        data = minimal_data()
        data["presets"] = {
            "motion-basic": "testsrc2=s={missing}:rate={fps}:duration={sample_s}",
        }
        plan = benchplan.build_plan(data, "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with self.assertRaisesRegex(benchplan.BenchPlanError, "preset motion-basic.*template"):
                benchplan.write_ninja(plan, out, out, benchplan.ATOMIC_HELPER)

    def test_render_print_uses_explicit_flags(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                rc = benchplan.main(
                    [
                        "render",
                        tmp,
                        "--codec",
                        "hevc",
                        "--w",
                        "1920",
                        "--h",
                        "1080",
                        "--fps",
                        "60",
                        "--kbps",
                        "30000",
                        "--sample-s",
                        "10",
                        "--print",
                    ]
                )

            output = stdout.getvalue()
            self.assertEqual(rc, 0)
            self.assertIn("Output:", output)
            self.assertIn("hevc_1920x1080_60fps_30000kbps_motion-basic_dur10s.hevc", output)
            self.assertIn("ffmpeg", output)
            self.assertIn("testsrc2", output)

    def test_render_unknown_preset_fails_fast(self) -> None:
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            rc = benchplan.main(
                [
                    "render",
                    "build/one-off",
                    "--codec",
                    "hevc",
                    "--w",
                    "1920",
                    "--h",
                    "1080",
                    "--fps",
                    "60",
                    "--kbps",
                    "30000",
                    "--preset",
                    "weird",
                    "--print",
                ]
            )

        self.assertEqual(rc, 2)
        self.assertIn("render.preset", stderr.getvalue())

    @mock.patch("benchplan.subprocess.run")
    def test_render_invokes_atomic_helper(self, run_mock: mock.Mock) -> None:
        media = benchplan.MediaSpec(
            codec="hevc",
            w=1920,
            h=1080,
            fps=60,
            kbps=30000,
            preset="motion-basic",
            sample_s=10,
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with contextlib.redirect_stdout(io.StringIO()):
                benchplan.render_command(out, media, benchplan.read_presets({}), False)

            run_mock.assert_called_once()
            argv = run_mock.call_args.args[0]
            self.assertTrue(run_mock.call_args.kwargs["check"])
            self.assertEqual(argv[0], "bash")
            self.assertIn("write-output-atomically.sh", argv[1])
            self.assertIn(str(out.resolve() / media.filename), argv)

    @mock.patch("benchplan.run_ninja")
    def test_generate_writes_artifacts_and_runs_ninja(self, run_ninja_mock: mock.Mock) -> None:
        data = minimal_data()
        plan = benchplan.build_plan(data, "bundled")
        expected_targets = sorted(
            [f"samples/{filename}" for filename in plan.media_jobs]
            + [f"samples/{filename}" for filename in plan.title_jobs]
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")

            with contextlib.redirect_stdout(io.StringIO()):
                benchplan.generate_command("bundled", benchset, out, out, 2, False)

            run_ninja_mock.assert_called_once_with(out, Path(".benchgen/build.ninja"), 2, expected_targets)
            self.assertTrue((out / "suites" / "quick-smoke.bench").exists())
            self.assertTrue((out / ".benchgen" / "build.ninja").exists())
            self.assertTrue((out / ".benchgen" / "manifest.json").exists())
            for target in expected_targets:
                state_path = out / ".benchgen" / "applied" / f"{target}.sig"
                self.assertTrue(state_path.exists())

    @mock.patch("benchplan.run_ninja")
    def test_generate_bootstraps_legacy_current_outputs_without_running_ninja(self, run_ninja_mock: mock.Mock) -> None:
        data = minimal_data()
        plan = benchplan.build_plan(data, "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")

            benchplan.write_suite_files(plan, out)
            samples_dir = out / "samples"
            samples_dir.mkdir(parents=True)
            jobs = []
            for filename, media in sorted(plan.media_jobs.items()):
                (samples_dir / filename).write_text("cached", encoding="utf-8")
                jobs.append({"kind": "sample", "path": f"samples/{filename}", "signature": media.signature})
            for filename, title in sorted(plan.title_jobs.items()):
                (samples_dir / filename).write_text("cached", encoding="utf-8")
                jobs.append({"kind": "title", "path": f"samples/{filename}", "signature": title.signature})

            legacy_manifest = {
                "version": 1,
                "selector": "bundled",
                "suites": [suite.name for suite in plan.suites],
                "jobs": jobs,
            }
            metadata_dir = out / ".benchgen"
            metadata_dir.mkdir(parents=True)
            (metadata_dir / "manifest.json").write_text(json.dumps(legacy_manifest), encoding="utf-8")

            with contextlib.redirect_stdout(io.StringIO()):
                benchplan.generate_command("bundled", benchset, out, out, 2, False)

            run_ninja_mock.assert_not_called()
            for job in jobs:
                state_path = metadata_dir / "applied" / f"{job['path']}.sig"
                self.assertTrue(state_path.exists())

    def test_status_warns_for_dirty_present_outputs(self) -> None:
        data = minimal_data()
        plan = benchplan.build_plan(data, "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")

            benchplan.write_suite_files(plan, out)
            samples_dir = out / "samples"
            samples_dir.mkdir(parents=True)
            for filename in plan.media_jobs:
                (samples_dir / filename).write_text("cached", encoding="utf-8")
            for filename in plan.title_jobs:
                (samples_dir / filename).write_text("cached", encoding="utf-8")

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                rc = benchplan.status_command("bundled", benchset, out, out, False, False)

            self.assertEqual(rc, 0)
            self.assertIn("Warning: bench content is dirty", stdout.getvalue())

    def test_status_check_ready_fails_when_required_outputs_are_missing(self) -> None:
        data = minimal_data()

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                rc = benchplan.status_command("bundled", benchset, out, out, True, False)

            self.assertEqual(rc, 1)
            self.assertIn("Bench content is not ready", stdout.getvalue())

    def test_prune_stale_applied_state_removes_unreferenced_signatures(self) -> None:
        plan = benchplan.build_plan(minimal_data(), "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            stale_state = out / ".benchgen" / "applied" / "samples" / "old-sample.h264.sig"
            stale_state.parent.mkdir(parents=True)
            stale_state.write_text("old", encoding="utf-8")

            removed = benchplan.prune_stale_applied_signature_state(plan, out, out, False)

            self.assertEqual(removed, [Path("applied") / "samples" / "old-sample.h264.sig"])
            self.assertFalse(stale_state.exists())

    def test_prune_stale_assets_removes_only_unrefd_generated_files(self) -> None:
        plan = benchplan.build_plan(minimal_data(), "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchplan.write_suite_files(plan, out)
            samples_dir = out / "samples"
            samples_dir.mkdir(parents=True)
            current_sample = next(iter(plan.media_jobs))
            current_title = next(iter(plan.title_jobs))
            keep_paths = [
                out / "suites" / "quick-smoke.bench",
                samples_dir / current_sample,
                samples_dir / current_title,
                samples_dir / "manual-note.txt",
            ]
            stale_paths = [
                out / "suites" / "old-suite.bench",
                samples_dir / "old-sample.h264",
                samples_dir / "old-title.hevc",
            ]
            for path in keep_paths + stale_paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("cached", encoding="utf-8")

            removed = benchplan.prune_stale_assets(plan, out, False)

            self.assertEqual([path.as_posix() for path in removed], [
                "samples/old-sample.h264",
                "samples/old-title.hevc",
                "suites/old-suite.bench",
            ])
            for path in keep_paths:
                self.assertTrue(path.exists())
            for path in stale_paths:
                self.assertFalse(path.exists())

    def test_prune_stale_assets_dry_run_keeps_files(self) -> None:
        data = minimal_data()
        plan = benchplan.build_plan(data, "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            stale = out / "samples" / "old-sample.h264"
            stale.parent.mkdir(parents=True)
            stale.write_text("cached", encoding="utf-8")

            removed = benchplan.prune_stale_assets(plan, out, True)

            self.assertEqual(removed, [Path("samples") / "old-sample.h264"])
            self.assertTrue(stale.exists())

    @mock.patch("benchplan.run_ninja")
    def test_generate_can_prune_stale_assets(self, run_ninja_mock: mock.Mock) -> None:
        data = minimal_data()
        plan = benchplan.build_plan(data, "bundled")
        expected_targets = sorted(
            [f"samples/{filename}" for filename in plan.media_jobs]
            + [f"samples/{filename}" for filename in plan.title_jobs]
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            benchset = out / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")
            stale = out / "samples" / "old-sample.h264"
            stale.parent.mkdir(parents=True)
            stale.write_text("cached", encoding="utf-8")

            with contextlib.redirect_stdout(io.StringIO()):
                benchplan.generate_command("bundled", benchset, out, out, 2, True)

            run_ninja_mock.assert_called_once_with(out, Path(".benchgen/build.ninja"), 2, expected_targets)
            self.assertFalse(stale.exists())

    @mock.patch("benchplan.run_ninja")
    def test_generate_uses_state_relative_ninja_targets(self, run_ninja_mock: mock.Mock) -> None:
        data = minimal_data()
        plan = benchplan.build_plan(data, "bundled")

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture_dir = root / "fixtures"
            state_dir = root / "state"
            benchset = root / "benchset.json"
            benchset.write_text(json.dumps(data), encoding="utf-8")

            with contextlib.redirect_stdout(io.StringIO()):
                benchplan.generate_command("bundled", benchset, fixture_dir, state_dir, 2, False)

            rel_outputs = sorted(
                [Path("samples") / filename for filename in plan.media_jobs]
                + [Path("samples") / filename for filename in plan.title_jobs],
                key=lambda path: path.as_posix(),
            )
            expected_targets = benchplan.ninja_targets_for_outputs(fixture_dir, state_dir, rel_outputs)
            run_ninja_mock.assert_called_once_with(state_dir, Path("build.ninja"), 2, expected_targets)


if __name__ == "__main__":
    unittest.main()
