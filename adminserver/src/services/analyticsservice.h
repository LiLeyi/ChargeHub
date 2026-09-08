#ifndef CHARGEHUB_ANALYTICSSERVICE_H
#define CHARGEHUB_ANALYTICSSERVICE_H
/** @file analyticsservice.h
 * @brief 驾驶舱查询、预测重算、风险告警和调度建议的边界。
 * refreshForecast 只重建分析派生表，不改变核心交易数据。
 */
#include <QJsonObject>
#include <QVariantMap>
#include <QVector>
class Database;
class AnalyticsService {
public:
 /** 只借用 Database，不拥有连接生命周期。 */
 explicit AnalyticsService(Database *db);
 /** 驾驶舱同步查询：营收、桩状态和站点空闲排行。 */
 QJsonObject salesSummary() const; QJsonObject pileStatusStats() const; QJsonObject cockpit() const;
 /** 分析派生表的只读结果。 */
 QVector<QVariantMap> listForecasts() const; QVector<QVariantMap> listHourlyLoad() const;
 QVector<QVariantMap> listFaultRisks() const; QVector<QVariantMap> listAlerts() const;
 QVector<QVariantMap> listDispatchPlan() const; QVariantMap latestReport() const;
 /**
  * 清空并重建分析派生表。只读取订单/电桩事实数据，不修改余额、订单或桩状态。
  * 返回写入的多时间跨度预测记录数，供管理界面显示刷新结果。
  */
 int refreshForecast();
private: Database *db_;
};
#endif
