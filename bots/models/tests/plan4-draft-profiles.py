"""Check draft-profile and runtime entrypoint wiring of every models bot; no builds or native games."""
from pathlib import Path
import re
import unittest


SUITE = Path(__file__).resolve().parents[1]  # bots/models
RUNTIMES = {
    "foundry": "Foundry", "governor": "Governor", "habitat": "Habitat",
    "repairman": "Repairman", "archipelago": "Archipelago",
    "timetable": "Timetable", "stewards": "Stewards", "bulkheads": "Bulkheads",
    "chronicle": "Chronicle", "quotient": "Quotient", "counterpart": "Counterpart",
    "kernel": "KernelLive", "capital": "CapitalLive", "commons": "Commons",
    "metronome": "SdkMetronome", "covenant": "Covenant",
}


def source(path):
    text = (SUITE / path).read_text(encoding="utf-8")
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def definitions(text, macro):
    return list(re.finditer(r"^\s*#\s*define\s+" + re.escape(macro)
                           + r"[ \t]+([^\n]+)$", text, re.M))


def draft_body(text):
    start = text.index("void draft() {") + len("void draft() {")
    depth = 1
    for end in range(start, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[start:end]
    raise AssertionError("Unclosed legacy draft function")


def mode_zero_draft(body, profile_defined):
    """Select only the known draft preprocessor conditions, failing closed."""
    frames = []
    result = []
    for line in body.splitlines():
        directive = re.match(r"^\s*#\s*(if|ifdef|else|endif)\b(.*)$", line)
        if directive:
            kind, expression = directive.groups()
            expression = expression.strip()
            if kind in ("if", "ifdef"):
                if kind == "if" and expression == "PLAN4_MACRO_MODE":
                    condition = False
                elif kind == "ifdef" and expression == "PLAN4_DRAFT_PROFILE":
                    condition = profile_defined
                else:
                    raise AssertionError("Unrecognized legacy draft condition: " + line.strip())
                frames.append([condition, False])
            elif kind == "else":
                if not frames or frames[-1][1]:
                    raise AssertionError("Unexpected legacy draft #else")
                frames[-1] = [not frames[-1][0], True]
            elif not frames:
                raise AssertionError("Unexpected legacy draft #endif")
            else:
                frames.pop()
        elif line.lstrip().startswith("#"):
            raise AssertionError("Unsupported legacy draft directive: " + line.strip())
        elif all(active for active, _ in frames):
            result.append(line)
    if frames:
        raise AssertionError("Unclosed legacy draft condition")
    return "\n".join(result)


class DraftProfileIntegrationTest(unittest.TestCase):
    def assert_define(self, text, macro, expected, before):
        matches = definitions(text, macro)
        self.assertEqual(len(matches), 1, macro + " must have exactly one explicit definition")
        self.assertEqual(matches[0].group(1).strip(), expected, macro)
        self.assertLess(matches[0].start(), before, macro + " must precede the shared entrypoint")
        self.assertNotRegex(text, r"(?m)^\s*#\s*undef\s+" + re.escape(macro) + r"\b")
        # Thin entrypoints must not hide their profile/flag under a conditional.
        self.assertNotRegex(text[:before], r"(?m)^\s*#\s*(?:if|ifdef|ifndef|elif|else|endif)\b")

    def assert_include(self, text, target):
        matches = list(re.finditer(r'^\s*#\s*include\s+"' + re.escape(target) + r'"\s*$', text, re.M))
        self.assertEqual(len(matches), 1, "Expected one shared entrypoint include: " + target)
        return matches[0].start()

    def test_suite_bots_are_the_reference_and_the_registered_models(self):
        bots = {path.parent.parent.name for path in SUITE.glob("*/src/main.cpp")}
        self.assertEqual(bots, set(RUNTIMES) | {"reference"},
                         "Every bots/models/<bot> must be the reference or a model registered here")

    def test_every_model_selects_its_own_profile_and_runtime(self):
        draft = source("common/draft.h")
        enum = re.search(r"enum\s+class\s+Profile\s*\{([^}]+)\}", draft)
        self.assertIsNotNone(enum, "Profile enum missing")
        names = [name.strip() for name in enum.group(1).split(",") if name.strip()]
        self.assertEqual(len(names), len(set(names)), "Duplicate profile enum member")
        slugs = set(names) - {"reference", "count"}
        self.assertEqual(slugs, set(RUNTIMES), "Every model profile needs a registered runtime")
        for slug in sorted(slugs):
            path = f"{slug}/src/main.cpp"
            with self.subTest(path=path):
                text = source(path)
                include = self.assert_include(text, "../../common/model-main.h")
                self.assert_define(text, "PLAN4_DRAFT_PROFILE", "plan4::drafting::Profile::" + slug, include)
                self.assert_define(text, "PLAN4_RUNTIME_TYPE", "plan4::runtime::" + RUNTIMES[slug], include)
                self.assert_define(text, "PLAN4_MODEL_ENABLED", "1", include)
                self.assertNotIn("Profile::reference", text)

    def test_reference_selects_reference_explicitly(self):
        text = source("reference/src/main.cpp")
        include = self.assert_include(text, "../../common/legacy.h")
        self.assert_define(text, "PLAN4_MACRO_MODE", "1", include)
        self.assert_define(text, "PLAN4_DRAFT_PROFILE", "plan4::drafting::Profile::reference", include)
        self.assertRegex(text, r"plan4_legacy::draft\s*\(\s*\)")

    def test_mode_zero_legacy_ignores_profile_overrides(self):
        # No suite bot runs mode 0 (the B0 extraction); this guards the shared draft body only.
        body = draft_body(source("common/legacy.h"))
        ordinary = mode_zero_draft(body, profile_defined=False)
        overridden = mode_zero_draft(body, profile_defined=True)
        self.assertEqual(ordinary, overridden, "Profile overrides changed mode-zero draft")
        self.assertNotIn("PLAN4_DRAFT_PROFILE", overridden)
        self.assertNotIn("plan4::drafting", overridden)
        order = re.findall(r"cambc::champion_draft::([a-z_]+)", overridden)
        self.assertEqual(order, ["tavi", "varg", "vendetta", "vesper_vale", "marcella"])
        self.assertIn("cambc::draft::current_draft(team::ally)", overridden)
        self.assertIn("cambc::draft::pick(choice)", overridden)


if __name__ == "__main__":
    unittest.main()
