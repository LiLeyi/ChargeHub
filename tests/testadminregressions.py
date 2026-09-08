# -*- coding: utf-8 -*-
"""管理端关键回归检查，不依赖正在运行的 Qt 服务。"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AdminRegressionTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_admin_window_does_not_refresh_forecast_during_startup(self) -> None:
        window = self.read("adminserver/src/mainwindow.cpp")
        self.assertEqual(window.count("dispatch_->refreshForecast();"), 1)

    def test_order_search_includes_pile_number(self) -> None:
        service = self.read("adminserver/src/services/adminservice.cpp")
        self.assertIn("OR p.pile_no LIKE ?", service)

    def test_station_creation_checks_transaction_result(self) -> None:
        service = self.read("adminserver/src/services/adminservice.cpp")
        self.assertIn("const bool ok = db_->transaction([&]", service)
        self.assertIn("return ok ? sid : 0;", service)


if __name__ == "__main__":
    unittest.main()
