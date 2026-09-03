# -*- coding: utf-8 -*-
"""Fast checks for the configurable dashboard soak runner."""
from __future__ import annotations

import unittest

from soaktest import runSoak, validateSnapshot
from testsupport import TemporaryDatabase


class DashboardSoakTest(unittest.TestCase):
    def testShortSoakUsesTheRealDashboardRoute(self) -> None:
        with TemporaryDatabase(withHistory=True) as databasePath:
            result = runSoak(
                databasePath,
                durationSeconds=0.04,
                intervalSeconds=0.005,
            )

        self.assertTrue(result["passed"], result)
        self.assertGreaterEqual(result["attempts"], 2)
        self.assertEqual(result["failures"], 0)

    def testSnapshotValidationRejectsAnIncompleteContract(self) -> None:
        with self.assertRaisesRegex(ValueError, "revenue trend"):
            validateSnapshot(
                {
                    "schemaVersion": "1.0",
                    "source": "live",
                    "revenueTrend": {"points": []},
                    "hourlyCharge": [{}] * 24,
                }
            )


if __name__ == "__main__":
    unittest.main()
