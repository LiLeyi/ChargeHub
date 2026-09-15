-- ChargeHub SQLite 表结构（权威说明，与 C++ Database::open 建表一致）
--
-- 库文件：管理端可执行文件旁 data/chargehub.db
-- 写权限：仅 PC 管理端 Dispatch。用户端禁止打开本文件。
-- 大屏 Flask 只读；ml/forecast.py 只写分析相关表，不准改余额/桩状态/订单。
-- 关系：admin 独立；user 1—1 user_avatar；station 1—* pile；
--       user/pile 1—* charge_order / reservation / station_review
-- 金额：库里的 balance / amount / price_per_kwh 对外仍是「元」；
--       C++ 内部加减先换成整数「分」，写回时再换成元。

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- ---------------------------------------------------------------------------
-- admin：运营人员账号。只给管理端登录框用，不走 8888。
-- 车主不能用这张表登录；管理员也不能用 user 表进运营窗口。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS admin (
    id            INTEGER PRIMARY KEY AUTOINCREMENT, -- 管理员编号
    username      TEXT    NOT NULL UNIQUE,           -- 登录名，演示账号 admin
    password_hash TEXT    NOT NULL,                  -- SHA256 十六进制，不存明文
    created_at    TEXT    NOT NULL                   -- 注册时间 yyyy-MM-dd HH:mm:ss
);

-- ---------------------------------------------------------------------------
-- user：车主档案。余额、冻结、注销都在这一行。
-- status：正常=可登录可充电；冻结=可看历史但不可操作；注销=留档，手机号不能再注册。
-- loc_*：用户端选地址后记下的定位，找附近电站用。默认北京某点，仅演示。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS user (
    id            INTEGER PRIMARY KEY AUTOINCREMENT, -- 用户编号，订单/预约都引用它
    phone         TEXT    NOT NULL UNIQUE,           -- 登录手机号，11 位
    nickname      TEXT    NOT NULL,                  -- 显示名，注册默认「用户+后四位」
    avatar_path   TEXT    NOT NULL DEFAULT '',       -- 空=无头像；'db'=头像在 user_avatar
    password_hash TEXT    NOT NULL DEFAULT '',       -- SHA256，登录比对
    balance       REAL    NOT NULL DEFAULT 0.00,     -- 余额，单位元；扣费在 settle
    status        TEXT    NOT NULL DEFAULT '正常' CHECK (status IN ('正常', '冻结', '注销')),
    created_at    TEXT    NOT NULL,                  -- 注册时间
    address       TEXT    NOT NULL DEFAULT '',       -- 用户选的定位地址文字
    loc_lat       REAL    NOT NULL DEFAULT 39.9644,  -- 纬度
    loc_lng       REAL    NOT NULL DEFAULT 116.3473, -- 经度
    close_reason  TEXT    NOT NULL DEFAULT '',       -- 注销原因（界面可留空）
    closed_at     TEXT    NOT NULL DEFAULT ''        -- 注销时间；未注销则为空
);

-- ---------------------------------------------------------------------------
-- user_avatar：头像二进制。一行对一个用户。
-- 仅 Dispatch::updateProfile 写入。回包时压成 Base64 给用户端，不走文件路径。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS user_avatar (
    user_id    INTEGER PRIMARY KEY REFERENCES user(id), -- 对应用户
    mime       TEXT    NOT NULL,                        -- 如 image/jpeg
    data       BLOB    NOT NULL,                        -- 压缩后的图片字节
    updated_at TEXT    NOT NULL                         -- 最近一次更换时间
);

-- ---------------------------------------------------------------------------
-- station：充电站。一座站有多根桩，一份基准电价。
-- price_per_kwh：没有 tariff_rule 时，calcLive 就用这个单价。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS station (
    id            INTEGER PRIMARY KEY AUTOINCREMENT, -- 电站编号
    name          TEXT    NOT NULL,                  -- 站名，界面和大屏显示
    address       TEXT    NOT NULL,                  -- 地址文字，找站、导航用
    lng           REAL    NOT NULL,                  -- 经度
    lat           REAL    NOT NULL,                  -- 纬度
    price_per_kwh REAL    NOT NULL CHECK (price_per_kwh > 0) -- 基准电价，元/度
);

-- ---------------------------------------------------------------------------
-- pile：电桩。状态机：闲置 ↔ 在用；运营可标故障再恢复。
-- 开充时改为在用；停充/断线/代停改回闲置。故障桩不能开充。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS pile (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT, -- 桩编号，开充传 pileId
    pile_no              TEXT    NOT NULL UNIQUE,           -- 对外桩号，如 A-01
    station_id           INTEGER NOT NULL REFERENCES station(id), -- 所属电站
    type                 TEXT    NOT NULL CHECK (type IN ('快充', '慢充')),
    power_kw             REAL    NOT NULL CHECK (power_kw > 0), -- 额定功率，计费用
    status               TEXT    NOT NULL DEFAULT '闲置' CHECK (status IN ('闲置', '在用', '故障')),
    total_charge_count   INTEGER NOT NULL DEFAULT 0,        -- 累计充电次数（演示/统计）
    total_charge_minutes INTEGER NOT NULL DEFAULT 0,        -- 累计充电分钟
    last_seen_at         TEXT    NOT NULL DEFAULT '',       -- 最近一次被开充/操作的时间
    fault_code           TEXT    NOT NULL DEFAULT '',       -- 故障码，运营标记时写入
    fault_at             TEXT    NOT NULL DEFAULT ''        -- 标记故障的时间
);

-- ---------------------------------------------------------------------------
-- charge_order：充电订单。全组唯一账本，只允许 Dispatch 写。
-- 状态：充电中 → 待结算 → 已完成。已取消预留，当前主路径不用。
-- 部分唯一索引保证：一用户同时最多一笔未完成；一桩同时最多一笔充电中。
-- energy_kwh / amount 在停充、结算时由 calcLive 写入，单位仍是度和元。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS charge_order (
    id          INTEGER PRIMARY KEY AUTOINCREMENT, -- 订单编号
    order_no    TEXT    NOT NULL UNIQUE,           -- 对外单号，如 CH20260905...
    user_id     INTEGER NOT NULL REFERENCES user(id),
    pile_id     INTEGER NOT NULL REFERENCES pile(id),
    status      TEXT    NOT NULL CHECK (status IN ('充电中', '待结算', '已完成', '已取消')),
    start_time  TEXT    NOT NULL,                  -- 开充时刻
    end_time    TEXT,                              -- 停充时刻；充电中为空
    energy_kwh  REAL    NOT NULL DEFAULT 0,        -- 电量（度），停充后落库
    amount      REAL    NOT NULL DEFAULT 0,        -- 费用（元），停充后落库
    created_at  TEXT    NOT NULL                   -- 下单时间，通常等于 start_time
);

-- ---------------------------------------------------------------------------
-- recharge_log：充值流水。Dispatch::recharge 在加余额的同一事务里插入。
-- result 一般为「成功」；失败的请求不会写这一行。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS recharge_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user(id),
    amount     REAL    NOT NULL,                  -- 充值金额，元；单笔上限 10000
    result     TEXT    NOT NULL,                  -- 结果文字
    created_at TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- audit_log：运营操作留痕。重启、标故障、冻结、代结算、断线释放都会写。
-- 车主自己的开充/结算不记这里，那些看 charge_order。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS audit_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    actor      TEXT    NOT NULL,  -- 谁做的，如 admin 或 system
    action     TEXT    NOT NULL,  -- 动作名，如 远程重启 / 代结算 / 断线释放
    target     TEXT    NOT NULL,  -- 对象，如桩号、用户 id、订单号
    result     TEXT    NOT NULL,  -- 结果摘要
    created_at TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- load_forecast：负荷预测结果。refreshForecast 或 ml/forecast.py 写入。
-- 大屏 /api/analysis、管理端智能分析页只读。不参与计费。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS load_forecast (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id    INTEGER NOT NULL REFERENCES station(id), -- 哪一座站
    horizon_hours INTEGER NOT NULL,  -- 预测窗口：1 / 6 / 24 小时
    pred_kwh      REAL    NOT NULL,  -- 预计用电量（度）
    pred_idle     INTEGER NOT NULL,  -- 预计还能空出几根桩
    peak_hour     TEXT    NOT NULL,  -- 窗口内预计高峰时刻，如 18:00
    created_at    TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- reservation：预约。有效期内别人不能占用该桩（除非预约人自己开充）。
-- expireReservations 把过期行标 no_show=1，status 不再当「有效」。
-- 开充成功则 status 改为已履约。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS reservation (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user(id),
    pile_id    INTEGER NOT NULL REFERENCES pile(id),
    status     TEXT    NOT NULL DEFAULT '有效' CHECK (status IN ('有效', '已取消', '已履约')),
    expire_at  TEXT    NOT NULL,                 -- 过期时刻；过了别人就能用
    created_at TEXT    NOT NULL,
    no_show    INTEGER NOT NULL DEFAULT 0        -- 1=超时未到，算爽约
);

-- ---------------------------------------------------------------------------
-- station_review：用户对电站/桩的评分和文字评价。
-- 必须有文字才能提交。pile_id=0 表示只评站（旧数据迁移后会补一根基桩）。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS station_review (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user(id),
    station_id INTEGER NOT NULL REFERENCES station(id),
    pile_id    INTEGER NOT NULL DEFAULT 0,       -- 评的哪根桩；0 表示未指定
    score      INTEGER NOT NULL CHECK (score BETWEEN 1 AND 5), -- 1~5 星
    comment    TEXT    NOT NULL DEFAULT '',      -- 评价正文，提交时不允许空
    created_at TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- review_doc：评价文本再存一份，给情感统计 / 关键词用。
-- 同一用户对同一桩只保留一行（唯一索引）。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS review_doc (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id    INTEGER NOT NULL, -- 哪根桩
    user_id    INTEGER NOT NULL, -- 哪位用户
    doc        TEXT    NOT NULL, -- 评价原文
    created_at TEXT    NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_review_doc_user_pile ON review_doc(user_id, pile_id);

-- ---------------------------------------------------------------------------
-- hourly_load：分时负荷预测（0~23 点）。refreshForecast 重算前会清空再写。
-- 管理端折线图、大屏用。不参与真实扣费。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS hourly_load (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL, -- 电站
    hour       INTEGER NOT NULL, -- 0~23
    pred_kwh   REAL    NOT NULL, -- 该小时预计电量
    created_at TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- fault_risk：电桩故障风险打分。分析页「风险」表。只由预测/refreshForecast 写。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS fault_risk (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no    TEXT    NOT NULL, -- 桩号
    station    TEXT    NOT NULL, -- 站名（冗余，方便直接展示）
    score      REAL    NOT NULL, -- 风险分数，越大越危险
    level      TEXT    NOT NULL, -- 高 / 中 / 低
    reason     TEXT    NOT NULL, -- 一句话原因
    created_at TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- analysis_alert：分析告警（高峰将至、某站过载等）。只展示，不自动改价。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS analysis_alert (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    level      TEXT    NOT NULL, -- 告警级别
    title      TEXT    NOT NULL, -- 标题
    detail     TEXT    NOT NULL, -- 详情
    created_at TEXT    NOT NULL
);

-- ---------------------------------------------------------------------------
-- dispatch_plan：调度建议（例如某站峰价上浮）。运营点「采纳」才真正改 tariff_rule。
-- adopted=1 表示已经采纳过，adopted_at 记下时间。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS dispatch_plan (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    station    TEXT    NOT NULL,                 -- 站名
    recommend  REAL    NOT NULL,                 -- 建议电价（元/度）或上浮幅度
    priority   INTEGER NOT NULL,                 -- 优先级，数字越小越先看
    reason     TEXT    NOT NULL,                 -- 建议原因
    created_at TEXT    NOT NULL,
    adopted    INTEGER NOT NULL DEFAULT 0,       -- 0 未采纳，1 已采纳
    adopted_at TEXT    NOT NULL DEFAULT ''       -- 采纳时间
);

-- ---------------------------------------------------------------------------
-- tariff_rule：分时电价。start_hour 含、end_hour 不含。
-- 例：17~22 为峰。无规则时 calcLive 退回 station.price_per_kwh。
-- 只由 applyDefaultTariff / adoptDispatchPlan 写入。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS tariff_rule (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id    INTEGER NOT NULL REFERENCES station(id),
    start_hour    INTEGER NOT NULL, -- 起始小时 0~23，含
    end_hour      INTEGER NOT NULL, -- 结束小时 1~24，不含
    price_per_kwh REAL    NOT NULL CHECK (price_per_kwh > 0), -- 该时段单价，元/度
    label         TEXT    NOT NULL DEFAULT ''                  -- 谷 / 平 / 峰
);

-- ---------------------------------------------------------------------------
-- session：用户端登录 token 落库。管理端重启后 30 分钟内仍可用。
-- issueToken 写入；过期或 dropUser / 注销时删除。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS session (
    token      TEXT    PRIMARY KEY,              -- 随机通行证
    user_id    INTEGER NOT NULL REFERENCES user(id),
    updated_at TEXT    NOT NULL                  -- 签发或续期时间
);

-- ---------------------------------------------------------------------------
-- analysis_report：最近一次预测模型的评价指标（MAE/RMSE）。
-- refreshForecast 重算时先清空再插一行，管理端驾驶舱展示。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS analysis_report (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    model_version TEXT    NOT NULL, -- 模型名或版本字串
    mae           REAL    NOT NULL, -- 平均绝对误差
    rmse          REAL    NOT NULL, -- 均方根误差
    sample_n      INTEGER NOT NULL, -- 参与评估的样本数
    weather       TEXT    NOT NULL, -- 当时假定天气（演示用）
    created_at    TEXT    NOT NULL
);

-- 查某用户未完成订单、按时间列订单
CREATE INDEX IF NOT EXISTS idx_order_user_status ON charge_order(user_id, status);
CREATE INDEX IF NOT EXISTS idx_order_start ON charge_order(start_time);
-- 按站列桩
CREATE INDEX IF NOT EXISTS idx_pile_station ON pile(station_id);
-- 安全带：同一用户不能同时有两笔充电中或待结算
CREATE UNIQUE INDEX IF NOT EXISTS idx_order_user_open ON charge_order(user_id)
    WHERE status IN ('充电中', '待结算');
-- 安全带：同一根桩不能同时有两笔充电中
CREATE UNIQUE INDEX IF NOT EXISTS idx_order_pile_charging ON charge_order(pile_id)
    WHERE status='充电中';
-- 查某桩当前有效预约、按时间翻审计
CREATE INDEX IF NOT EXISTS idx_reservation_pile_status ON reservation(pile_id, status);
CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_log(created_at);
