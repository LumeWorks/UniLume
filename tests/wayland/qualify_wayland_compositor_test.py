#!/usr/bin/env python3
"""Unit tests for deterministic parts of the Wayland qualification harness.

These tests cover the logic that decides what a run is allowed to claim: how a
corpus entry becomes ordered key events, how an incorrect observation is
classified, how the addon's diagnostic bundle is read, and which issue #58
criteria a result satisfies. They intentionally require no compositor.
"""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


HARNESS_PATH = (
    Path(__file__).resolve().parents[2]
    / "scripts"
    / "test"
    / "qualify_wayland_compositor.py"
)
SPEC = importlib.util.spec_from_file_location("qualify_wayland_compositor", HARNESS_PATH)
assert SPEC is not None and SPEC.loader is not None
HARNESS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = HARNESS
SPEC.loader.exec_module(HARNESS)


class DefectClassificationTest(unittest.TestCase):
    def test_exact_output_has_no_defect(self) -> None:
        self.assertEqual(HARNESS.classify_defect("tiếng Việt", "tiếng Việt"), "none")

    def test_missing_character_is_lost(self) -> None:
        self.assertEqual(HARNESS.classify_defect("tiếng", "ting"), "lost")

    def test_empty_observation_is_lost(self) -> None:
        self.assertEqual(HARNESS.classify_defect("tiếng", ""), "lost")

    def test_repeated_expected_character_is_duplicate(self) -> None:
        self.assertEqual(HARNESS.classify_defect("tiếng", "tiiếng"), "duplicate")

    def test_same_multiset_different_order_is_reordered(self) -> None:
        self.assertEqual(HARNESS.classify_defect("tiếng", "tiếgn"), "reordered")

    def test_unconsumed_delete_byte_is_corrupted(self) -> None:
        """A Backspace the input method failed to eat must not look like a loss."""
        self.assertEqual(HARNESS.classify_defect("tiến", "tiếng\x7f"), "corrupted")

    def test_substituted_character_is_corrupted_not_duplicate(self) -> None:
        self.assertEqual(HARNESS.classify_defect("abc", "axc"), "corrupted")


class InputSplitTest(unittest.TestCase):
    def test_plain_text_is_one_event(self) -> None:
        self.assertEqual(
            HARNESS.split_input("tieengs"), [("text", "tieengs")]
        )

    def test_backspace_marker_becomes_an_ordered_key_event(self) -> None:
        self.assertEqual(
            HARNESS.split_input("tieengs<BS>"),
            [("text", "tieengs"), ("key", "BackSpace")],
        )

    def test_interior_and_trailing_markers_preserve_order(self) -> None:
        self.assertEqual(
            HARNESS.split_input("ab<BS>cd<BS>"),
            [
                ("text", "ab"),
                ("key", "BackSpace"),
                ("text", "cd"),
                ("key", "BackSpace"),
            ],
        )

    def test_key_events_are_counted_per_keystroke(self) -> None:
        self.assertEqual(HARNESS.count_key_events("tieengs<BS>"), 8)


class InjectionArgumentTest(unittest.TestCase):
    def test_arguments_carry_delay_and_ordered_events(self) -> None:
        self.assertEqual(
            HARNESS.wtype_arguments("ab<BS>", 10),
            ["wtype", "-d", "10", "ab", "-k", "BackSpace"],
        )

    def test_burst_omits_the_delay_flag_that_wtype_rejects(self) -> None:
        """wtype refuses a zero delay, and omitting it sends with no sleep."""
        self.assertEqual(HARNESS.wtype_arguments("ab", 0), ["wtype", "ab"])

    def test_leading_dash_is_refused_rather_than_becoming_a_flag(self) -> None:
        with self.assertRaises(HARNESS.QualificationError):
            HARNESS.wtype_arguments("-k", 10)

    def test_composed_text_is_refused_as_input(self) -> None:
        """The corpus input column holds keystrokes, never Vietnamese output."""
        with self.assertRaises(HARNESS.QualificationError):
            HARNESS.wtype_arguments("tiếng", 10)

    def test_xdotool_targets_the_nested_compositor_in_order(self) -> None:
        self.assertEqual(
            HARNESS.xdotool_arguments("ab<BS>", 1, "0x200000"),
            [
                ["xdotool", "windowfocus", "--sync", "0x200000"],
                [
                    "xdotool",
                    "type",
                    "--clearmodifiers",
                    "--delay",
                    "1",
                    "--",
                    "ab",
                ],
                ["xdotool", "key", "--clearmodifiers", "BackSpace"],
            ],
        )

    def test_xdotool_refuses_composed_input(self) -> None:
        with self.assertRaises(HARNESS.QualificationError):
            HARNESS.xdotool_arguments("tiếng", 1, "0x200000")


class BrowserProbeStateTest(unittest.TestCase):
    def test_visible_and_committed_values_are_independent(self) -> None:
        state = HARNESS.BrowserProbeState("controlled-token")
        state.record("controlled-token", "visible", "tiếng Việt", 1)
        observed, settled_ns = state.wait_for("visible", "tiếng Việt", 0)
        self.assertEqual(observed, "tiếng Việt")
        self.assertGreater(settled_ns, 0)
        state.record("controlled-token", "committed", "tiếng Việt", 2)
        state.clear_commit()
        self.assertIsNone(state.committed)
        self.assertEqual(state.visible, "tiếng Việt")
        self.assertFalse(state.visible_composing)

    def test_visible_value_retains_browser_composition_state(self) -> None:
        state = HARNESS.BrowserProbeState("controlled-token")
        state.record(
            "controlled-token",
            "visible",
            "tiếng Việt",
            1,
            composing=True,
        )
        self.assertEqual(state.visible, "tiếng Việt")
        self.assertTrue(state.visible_composing)
        self.assertTrue(state.composition_seen)
        state.record(
            "controlled-token",
            "visible",
            "tiếng Việt",
            2,
            composing=False,
        )
        self.assertFalse(state.visible_composing)
        self.assertTrue(state.composition_seen)
        state.clear_commit()
        self.assertFalse(state.composition_seen)

    def test_late_browser_report_cannot_restore_old_composition(self) -> None:
        state = HARNESS.BrowserProbeState("controlled-token")
        state.record(
            "controlled-token",
            "visible",
            "",
            3,
            composing=False,
        )
        state.clear_commit()
        state.record(
            "controlled-token",
            "visible",
            "old preedit",
            2,
            composing=True,
        )
        self.assertEqual(state.visible, "")
        self.assertFalse(state.composition_seen)

    def test_wrong_token_is_rejected(self) -> None:
        state = HARNESS.BrowserProbeState("controlled-token")
        with self.assertRaises(ValueError):
            state.record("other-token", "visible", "", 1)

    def test_firefox_profile_exists_and_skips_first_run_ui(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "browser-profile"
            HARNESS.prepare_firefox_profile(profile)

            self.assertTrue(profile.is_dir())
            preferences = (profile / "user.js").read_text(encoding="utf-8")
            self.assertIn("browser.aboutwelcome.enabled", preferences)
            self.assertIn("browser.shell.checkDefaultBrowser", preferences)

    def test_firefox_profile_exists_and_skips_first_run_ui(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "browser-profile"
            HARNESS.prepare_firefox_profile(profile)

            self.assertTrue(profile.is_dir())
            preferences = (profile / "user.js").read_text(encoding="utf-8")
            self.assertIn("browser.aboutwelcome.enabled", preferences)
            self.assertIn("browser.shell.checkDefaultBrowser", preferences)


class DiagnosticBundleTest(unittest.TestCase):
    def test_missing_bundle_reports_why_it_is_unavailable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bundle = HARNESS.read_diagnostic_bundle(
                Path(directory) / "absent.json", wait_seconds=0
            )
        self.assertFalse(bundle["available"])
        self.assertIn("UNILUME_FCITX_DIAGNOSTICS", str(bundle["reason"]))

    def test_backend_path_and_capability_gates_are_summarized(self) -> None:
        payload = {
            "schema": 1,
            "session": "wayland",
            "unilume_version": "0.1.0-rc1",
            "fcitx_version": "5.1.7",
            "total_events": 3,
            "preedit_handoffs": 2,
            "fallbacks": 1,
            "backend_failures": 0,
            "capability_losses": 1,
            "events": [
                {"path": "direct", "capability": "none"},
                {"path": "direct", "capability": "none"},
                {"path": "preedit", "capability": "unavailable"},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unilume-diagnostic.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            bundle = HARNESS.read_diagnostic_bundle(path, wait_seconds=0)
        self.assertTrue(bundle["available"])
        self.assertEqual(bundle["session"], "wayland")
        self.assertEqual(bundle["preedit_handoffs"], 2)
        self.assertEqual(bundle["observed_paths"], {"direct": 2, "preedit": 1})
        self.assertEqual(bundle["observed_capability_gates"], {"unavailable": 1})

    def test_corrupt_bundle_is_an_error_not_a_silent_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unilume-diagnostic.json"
            path.write_text("{not json", encoding="utf-8")
            with self.assertRaises(HARNESS.QualificationError):
                HARNESS.read_diagnostic_bundle(path, wait_seconds=0)


def observation(
    name: str,
    expected: str,
    observed: str,
    before_boundary: str | None = None,
    preedit_active_before_boundary: bool = False,
) -> object:
    return HARNESS.ObservedScenario(
        name=name,
        method="telex",
        expected=expected,
        observed=observed,
        before_boundary=expected if before_boundary is None else before_boundary,
        preedit_active_before_boundary=preedit_active_before_boundary,
        key_events=len(expected),
        completion_ns=1000,
    )


class SummaryTest(unittest.TestCase):
    def test_defects_are_counted_by_class(self) -> None:
        summary = HARNESS.summarize(
            [
                observation("a", "tiếng", "tiếng"),
                observation("b", "tiếng", "ting"),
                observation("c", "tiếng", "tiiếng"),
            ]
        )
        self.assertEqual(summary["errors"], 2)
        self.assertEqual(summary["defects"], {"lost": 1, "duplicate": 1})

    def test_empty_observation_set_is_refused(self) -> None:
        with self.assertRaises(HARNESS.QualificationError):
            HARNESS.summarize([])

    def test_soak_failures_are_exact_and_bounded(self) -> None:
        rows = [
            observation("correct", "tiếng", "tiếng"),
            observation("first", "tiếng", "tieêng"),
            observation("second", "Việt", "Vieệt"),
        ]
        failures = HARNESS.failure_samples(rows, limit=1)
        self.assertEqual(len(failures), 1)
        self.assertEqual(failures[0]["scenario"], "first")
        self.assertEqual(failures[0]["expected"], "tiếng")
        self.assertEqual(failures[0]["observed"], "tieêng")

    def test_resource_growth_requires_a_material_sustained_trend(self) -> None:
        self.assertTrue(
            HARNESS.linear_growth([1000, 1300, 1600, 1900, 2200, 2500], 1024)
        )
        self.assertFalse(
            HARNESS.linear_growth([1000, 2500, 1500, 2200, 1600, 2000], 1024)
        )


def result_template(**overrides: object) -> dict[str, object]:
    result: dict[str, object] = {
        "environment": {"family": "wlroots"},
        "client_survived": True,
        "corpus": {
            "summary": {
                "errors": 0,
                "defects": {},
                "zero_preedit_observations": 3,
                "preedit_fallback_observations": 0,
            }
        },
        "burst": {"summary": {"errors": 0, "defects": {}}},
        "soak": None,
        "diagnostics": {
            "available": True,
            "session": "wayland",
            "backend_failures": 0,
            "observed_paths": {"direct": 3},
        },
    }
    result.update(overrides)
    return result


class QualificationVerdictTest(unittest.TestCase):
    def test_clean_run_passes_and_claims_only_its_own_family(self) -> None:
        verdict = HARNESS.evaluate(result_template())
        self.assertTrue(verdict["overall_pass"])
        self.assertEqual(verdict["unmet"], [])
        self.assertEqual(verdict["compositor_families_claimed"], ["wlroots"])

    def test_corpus_error_fails_the_run(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(
                corpus={
                    "summary": {
                        "errors": 1,
                        "defects": {"lost": 1},
                        "zero_preedit_observations": 2,
                        "preedit_fallback_observations": 0,
                    }
                }
            )
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("corpus_exact_output", verdict["unmet"])

    def test_burst_reordering_fails_even_when_the_corpus_passed(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(
                burst={"summary": {"errors": 1, "defects": {"reordered": 1}}}
            )
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("burst_no_lost_duplicate_reordered", verdict["unmet"])

    def test_unobservable_fallback_reason_fails_the_run(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(diagnostics={"available": False, "reason": "absent"})
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("fallback_reason_observable", verdict["unmet"])
        self.assertIn("no_backend_failures", verdict["unmet"])

    def test_x11_session_in_the_bundle_is_not_wayland_evidence(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(
                diagnostics={
                    "available": True,
                    "session": "x11",
                    "backend_failures": 0,
                }
            )
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("fallback_reason_observable", verdict["unmet"])

    def test_short_soak_is_reported_as_non_qualifying(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(
                soak={
                    "qualifying": False,
                    "summary": {"errors": 0, "defects": {}},
                    "rss_linear_growth": False,
                    "threads_linear_growth": False,
                }
            )
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("soak_qualifying_duration", verdict["unmet"])

    def test_qualifying_soak_rejects_linear_resource_growth(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(
                soak={
                    "qualifying": True,
                    "summary": {"errors": 0, "defects": {}},
                    "rss_linear_growth": True,
                    "threads_linear_growth": False,
                }
            )
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("soak_rss_not_linear", verdict["unmet"])
        self.assertNotIn("soak_correct", verdict["unmet"])


class CorpusContractTest(unittest.TestCase):
    def test_shared_corpus_entries_are_injectable(self) -> None:
        """Every shared corpus entry must survive the Wayland injection layer."""
        for method in ("telex", "vni", "viqr"):
            for scenario in HARNESS.load_corpus(HARNESS.DEFAULT_CORPUS, method):
                arguments = HARNESS.wtype_arguments(scenario.encoded_input, 10)
                self.assertEqual(arguments[:3], ["wtype", "-d", "10"])
                self.assertEqual(
                    HARNESS.wtype_arguments(scenario.encoded_input, 0)[0], "wtype"
                )
                self.assertGreater(HARNESS.count_key_events(scenario.encoded_input), 0)


class BackendPathTest(unittest.TestCase):
    def test_browser_composition_is_preedit_even_when_dom_value_is_visible(
        self,
    ) -> None:
        observed = observation(
            "browser-preedit",
            "tiếng",
            "tiếng",
            preedit_active_before_boundary=True,
        )
        self.assertFalse(observed.zero_preedit)

    def test_all_direct_observations_are_the_direct_path(self) -> None:
        summary = {
            "zero_preedit_observations": 6,
            "preedit_fallback_observations": 0,
        }
        self.assertEqual(HARNESS.observed_backend_path(summary), "direct")

    def test_all_fallback_observations_are_the_preedit_path(self) -> None:
        summary = {
            "zero_preedit_observations": 0,
            "preedit_fallback_observations": 6,
        }
        self.assertEqual(HARNESS.observed_backend_path(summary), "preedit")

    def test_both_paths_in_one_run_are_mixed(self) -> None:
        summary = {
            "zero_preedit_observations": 2,
            "preedit_fallback_observations": 4,
        }
        self.assertEqual(HARNESS.observed_backend_path(summary), "mixed")

    def test_trace_ignores_the_off_path_when_naming_the_backend(self) -> None:
        self.assertEqual(
            HARNESS.diagnostic_backend_path({"observed_paths": {"off": 3, "preedit": 61}}),
            "preedit",
        )

    def test_absent_trace_path_is_unknown(self) -> None:
        self.assertEqual(HARNESS.diagnostic_backend_path({}), "unknown")


class StressPhaseTest(unittest.TestCase):
    def test_stress_defects_do_not_change_the_verdict(self) -> None:
        """The gate is defined at 1 ms/key, so unbounded-speed findings inform only."""
        verdict = HARNESS.evaluate(result_template())
        self.assertTrue(verdict["overall_pass"])
        self.assertNotIn("stress", str(verdict["checks"]))

    def test_trace_path_must_match_what_the_client_experienced(self) -> None:
        verdict = HARNESS.evaluate(
            result_template(
                diagnostics={
                    "available": True,
                    "session": "wayland",
                    "backend_failures": 0,
                    # The addon claims direct replacement...
                    "observed_paths": {"direct": 6},
                },
                corpus={
                    "summary": {
                        "errors": 0,
                        "defects": {},
                        # ...but the client only ever saw text after a boundary.
                        "zero_preedit_observations": 0,
                        "preedit_fallback_observations": 6,
                    }
                },
            )
        )
        self.assertFalse(verdict["overall_pass"])
        self.assertIn("backend_path_agrees_with_client", verdict["unmet"])


class BurstRateTest(unittest.TestCase):
    def test_gated_burst_rate_uses_the_delay_flag(self) -> None:
        self.assertEqual(
            HARNESS.wtype_arguments("ab", 1), ["wtype", "-d", "1", "ab"]
        )

    def test_default_burst_rate_matches_the_documented_gate(self) -> None:
        arguments = HARNESS.parse_arguments([])
        self.assertEqual(arguments.burst_delay_milliseconds, 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
