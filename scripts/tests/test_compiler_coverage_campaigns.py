import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    "compiler_coverage_campaigns",
    Path(__file__).resolve().parents[1] / "compiler-coverage-campaigns.py",
)
campaigns = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = campaigns
SPEC.loader.exec_module(campaigns)


class CompilerCoverageCampaignTests(unittest.TestCase):
    def test_campaign_inventory_is_complete_and_unique(self):
        names = [campaign.name for campaign in campaigns.CAMPAIGNS]
        self.assertEqual(len(names), 40)
        self.assertEqual(len(set(names)), len(names))
        root = Path(__file__).resolve().parents[2]
        for campaign in campaigns.CAMPAIGNS:
            self.assertTrue((root / "scripts" / campaign.script).is_file())

    def test_initial_batch_uses_global_budget(self):
        ready, pending, available = campaigns.take_ready(
            list(campaigns.CAMPAIGNS), 24, 24, 4
        )
        allocations = {
            campaign.name: jobs for campaign, jobs in ready
        }
        self.assertEqual(sum(allocations.values()), 24)
        self.assertEqual(allocations["pointer-condition"], 2)
        self.assertEqual(allocations["symbol-insert"], 4)
        self.assertNotIn("directory", allocations)
        self.assertTrue(pending)
        self.assertEqual(available, 0)

    def test_small_budget_reduces_each_campaign_allocation(self):
        campaign = campaigns.CAMPAIGNS[1]
        self.assertEqual(
            campaigns.campaign_job_count(campaign, 3, 4), 3
        )

    def test_campaign_cap_is_respected(self):
        campaign = campaigns.CAMPAIGNS[0]
        self.assertEqual(
            campaigns.campaign_job_count(campaign, 24, 4), 2
        )

    def test_output_directory_and_flags_are_preserved(self):
        campaign = next(
            item for item in campaigns.CAMPAIGNS
            if item.name == "flagged-record"
        )
        root = Path("/repo")
        command = campaigns.campaign_command(
            campaign, 4, root, root / "build"
        )
        self.assertEqual(command[2:4], ["--jobs", "4"])
        self.assertIn("--skip-runtime", command)
        self.assertEqual(
            command[-2:],
            [
                "--output-dir",
                "/repo/build/flagged-record-wave24-audit",
            ],
        )

    def test_failure_does_not_skip_queued_campaigns(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scripts = root / "scripts"
            scripts.mkdir()
            (scripts / "fail.py").write_text(
                "import sys\nprint('failed as requested')\nsys.exit(3)\n"
            )
            (scripts / "pass.py").write_text(
                "import os\nprint(os.environ['LLVM_PROFILE_FILE'])\n"
            )
            selected = (
                campaigns.Campaign("fail", "fail.py"),
                campaigns.Campaign("pass", "pass.py"),
            )
            with mock.patch.object(campaigns, "emit_log"):
                result = campaigns.run_campaigns(
                    root,
                    root / "build",
                    root / "raw",
                    root / "logs",
                    1,
                    1,
                    selected,
                )
            self.assertEqual(result, 1)
            self.assertIn(
                "failed as requested",
                (root / "logs/fail.log").read_text(),
            )
            self.assertIn(
                "dcc-pass-%8m.profraw",
                (root / "logs/pass.log").read_text(),
            )


if __name__ == "__main__":
    unittest.main()
