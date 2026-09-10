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
 /** @param db 数据库依赖；只借用，不拥有连接生命周期。 */
 explicit AnalyticsService(Database *db);
 /** @return 今日、本月、累计营收电量及趋势点；算法聚合已完成订单。 */
 QJsonObject salesSummary() const;
 /** @return 闲置、在用、故障电桩计数及饼图数据。 */
 QJsonObject pileStatusStats() const;
 /** @return 营收、桩状态、站点排行和最近模型报告的驾驶舱快照。 */
 QJsonObject cockpit() const;
 /** 分析派生表的只读结果。 */
 /** @return 按时间排列的负荷预测行。 */
 QVector<QVariantMap> listForecasts() const;
 /** @return 0..23 时的分时负荷行。 */
 QVector<QVariantMap> listHourlyLoad() const;
 /** @return 按风险降序排列的电桩故障风险。 */
 QVector<QVariantMap> listFaultRisks() const;
 /** @return 当前分析告警。 */
 QVector<QVariantMap> listAlerts() const;
 /** @return 调度建议及其采纳状态。 */
 QVector<QVariantMap> listDispatchPlan() const;
 /** @return 最近模型 MAE/RMSE 报告；无记录时为空。 */
 QVariantMap latestReport() const;
 /**
  * 清空并重建分析派生表。只读取订单/电桩事实数据，不修改余额、订单或桩状态。
  * 返回写入的多时间跨度预测记录数，供管理界面显示刷新结果。
  */
 int refreshForecast();
private: Database *db_;
};
#endif
