# -*- coding: utf-8 -*-
"""Validate the optimization specification for the owned dashboard and wallet work."""
from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / "spec.md"


class OptimizationSpecificationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.content = SPEC.read_text(encoding="utf-8")
        cls.readme = (ROOT / "README.md").read_text(encoding="utf-8")

    def testSpecRecordsTheExistingBaselineAndOwnedScope(self) -> None:
        for phrase in [
            "现有原型基线",
            "Web运营大屏",
            "Qt用户端余额与模拟充值",
            "不重写无关模块",
        ]:
            self.assertIn(phrase, self.content)

    def testDashboardRequirementsCoverContractAndFailureRecovery(self) -> None:
        for phrase in [
            "GET /api/dashboard",
            "schemaVersion",
            "近7日/30日",
            "1小时、6小时、24小时",
            "同一时刻最多一个刷新请求",
            "保留最近一次成功快照",
        ]:
            self.assertIn(phrase, self.content)

    def testWalletRequirementsCoverMoneyTransactionAndIdempotency(self) -> None:
        for phrase in [
            "amountCents",
            "balanceCents",
            "requestId",
            "BEGIN IMMEDIATE",
            "同一事务",
            "uncertain",
        ]:
            self.assertIn(phrase, self.content)

    def testPlanAndAcceptanceCriteriaAreTraceable(self) -> None:
        for phase in range(7):
            self.assertIn(f"Phase {phase}", self.content)
        for acceptanceId in [
            "AC-WEB-01",
            "AC-WEB-10",
            "AC-WAL-01",
            "AC-WAL-06",
            "AC-ALL-01",
        ]:
            self.assertIn(acceptanceId, self.content)

    def testCoordinationDecisionsAreExplicit(self) -> None:
        for phrase in [
            "Qt5还是Qt6",
            "充值协议与数据库迁移",
            "手机号免密登录",
            "预测输出口径",
        ]:
            self.assertIn(phrase, self.content)

    def testReadmeLinksToTheOptimizationSpecification(self) -> None:
        self.assertIn("[spec.md](spec.md)", self.readme)

    def testPhaseOneTestEntrypointsAreDocumentedAndPresent(self) -> None:
        for relativePath in [
            "dashboard/package.json",
            "dashboard/tests/dashboardmodeltest.js",
            "tests/testdashboard.py",
            "tests/testsupport.py",
            "tests/tests.pro",
            "tests/wallettest.pro",
            "tests/wallettest.cpp",
        ]:
            self.assertTrue((ROOT / relativePath).is_file(), relativePath)
        self.assertIn("python3 -m unittest discover", self.readme)
        self.assertIn("npm --prefix dashboard test", self.readme)
        self.assertIn("nodejs npm", self.readme)
        rebuild = (ROOT / "scripts" / "rebuild.sh").read_text(encoding="utf-8")
        self.assertIn('cp -a "$SRC/tests" "$DST/"', rebuild)
        self.assertIn("[x] Phase 1", self.content)

    def testPhaseTwoBackendArtifactsAreDocumentedAndPresent(self) -> None:
        for relativePath in [
            "dashboard/dashboardrepository.py",
            "dashboard/data/dashboard.json",
        ]:
            self.assertTrue((ROOT / relativePath).is_file(), relativePath)
        appSource = (ROOT / "dashboard" / "app.py").read_text(encoding="utf-8")
        self.assertIn('@app.get("/api/dashboard")', appSource)
        self.assertIn("[x] Phase 2", self.content)

    def testPhaseThreeDashboardArtifactsAreDocumentedAndPresent(self) -> None:
        for relativePath in [
            "dashboard/dashboard.css",
            "dashboard/dashboard.js",
            "dashboard/dashboarddata.js",
            "dashboard/dashboardmodel.js",
            "dashboard/tests/dashboarddatatest.js",
            "dashboard/tests/dashboardpagetest.js",
        ]:
            self.assertTrue((ROOT / relativePath).is_file(), relativePath)
        pageSource = (ROOT / "dashboard" / "dashboard.js").read_text(encoding="utf-8")
        self.assertNotIn("innerHTML", pageSource)
        self.assertIn("?demo=1", self.readme)
        self.assertIn("[x] Phase 3", self.content)

    def testPhaseFourWalletServerArtifactsAreDocumentedAndPresent(self) -> None:
        for relativePath in [
            "adminserver/src/rechargetransaction.h",
            "adminserver/src/rechargetransaction.cpp",
            "tests/testwalletschema.py",
        ]:
            self.assertTrue((ROOT / relativePath).is_file(), relativePath)
        protocol = (ROOT / "protocol" / "messages.md").read_text(encoding="utf-8")
        for phrase in ["amountCents", "balanceCents", "requestId", "QUERY_RECHARGE", "QUERY_WALLET"]:
            self.assertIn(phrase, protocol)
        self.assertIn("[x] Phase 4", self.content)

    def testPhaseFiveWalletClientArtifactsAreDocumentedAndPresent(self) -> None:
        for relativePath in [
            "userclient/src/walletcontroller.h",
            "userclient/src/walletcontroller.cpp",
        ]:
            self.assertTrue((ROOT / relativePath).is_file(), relativePath)
        controller = (ROOT / "userclient" / "src" / "walletcontroller.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("parseAmountCents", controller)
        self.assertNotIn("value.toDouble", controller)
        self.assertIn("[x] Phase 5", self.content)
        for phrase in [
            "一次操作只创建一个请求编号",
            "结果不确定",
            "服务器权威余额",
        ]:
            self.assertIn(phrase, self.readme)

    def testPhaseSixDeliveryArtifactsAreDocumentedAndPresent(self) -> None:
        for relativePath in [
            "docs/releasechecklist.md",
            "tests/soaktest.py",
            "tests/testsoak.py",
        ]:
            self.assertTrue((ROOT / relativePath).is_file(), relativePath)
        self.assertFalse((ROOT / "docs" / "startdash.log").exists())
        ignoreRules = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("docs/*.log", ignoreRules)
        self.assertIn("*.pro.user", ignoreRules)
        self.assertIn("--duration-seconds 1800", self.readme)
        self.assertNotIn("/home/bit", self.readme)
        self.assertIn("[x] Phase 6", self.content)


if __name__ == "__main__":
    unittest.main()
