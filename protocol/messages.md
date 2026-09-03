# ChargeHub Socket 报文（用户端 ↔ 管理端）

传输：TCP。管理端监听 **所有网卡的 8888 端口**（`0.0.0.0:8888`）。  
用户端在登录页填写 `IP:8888`。同一台电脑填 `127.0.0.1:8888`，连组里服务器填那台电脑的局域网 IP。

帧：`4 字节大端长度 + UTF-8 JSON`（见 `common/protocol.*`）。

当前钱包报文版本为2，请求可在顶层携带整数 `protocolVersion: 2`。未携带该字段的现有客户端继续按版本1处理；服务端在迁移期仍接受旧 `RECHARGE.amount`，并为其生成内部请求编号。版本2客户端必须使用整数分和 `requestId`，缺少字段或携带不支持的版本时返回 `PROTOCOL_ERROR`。新增字段保持加法兼容，版本1客户端和服务端都应忽略不认识的字段，4字节长度前缀不变。

管理端 GUI **不走** 这套报文，同进程直接调 `Dispatch`。所以 **不能** 再开第二个管理端去「远程操作」同一台服务器——第二份管理端会自己再开一套库、再抢 8888。

## 请求

```json
{ "type": "LOGIN", "seq": 1, "role": "user", "token": "", "data": {} }
```

## 响应

```json
{ "type": "LOGIN", "seq": 1, "code": 0, "message": "登录成功", "data": {} }
```

`code==0` 成功。除 LOGIN/REGISTER 外，必须带登录返回的 `token`。钱包错误响应除数字 `code` 外还带稳定字符串 `errorCode`，客户端逻辑不得依赖中文 `message`。

常见 `code`：`0` 成功，`400` 参数错，`401` 未登录/密码错，`403` 冻结或已注销，`404` 找不到，`409` 冲突（重复注册、有进行中订单不能注销）。

## type 一览

| type | 登录后 | data 主要字段 | 说明 |
|------|--------|---------------|------|
| LOGIN | 否 | phone, password | 成功返回 user + token |
| REGISTER | 否 | phone, password | 已注销手机号 409 |
| UPDATE_PROFILE | 是 | nickname / avatarBase64 / clearAvatar / address | 改资料；住址会解析坐标 |
| RECHARGE | 是 | amountCents, requestId | 模拟充值；同一请求编号只入账一次 |
| QUERY_RECHARGE | 是 | requestId | 超时后查询原充值结果 |
| QUERY_WALLET | 是 | | 权威余额（分）及最近30条充值记录 |
| QUERY_STATIONS | 是 | address, radiusKm, lat, lng | 附近电站 + nearbyPiles |
| QUERY_PILES | 是 | stationId | 站内电桩 |
| START_CHARGE | 是 | pileId | 开始充电 |
| CHARGE_STATUS | 是 | | 进行中/待结算实时计费 |
| STOP_CHARGE | 是 | | 结束 → 待结算，桩改闲置 |
| SETTLE_ORDER | 是 | | 扣余额 |
| LIST_ORDERS | 是 | | 我的订单 |
| LIST_RECHARGE | 是 | | 充值记录 |
| RESERVE_PILE | 是 | pileId | 预约 15 分钟 |
| CANCEL_RESERVE | 是 | | 取消预约 |
| REVIEW_STATION | 是 | stationId, pileId, score, comment | 评价 |
| LIST_PILE_REVIEWS | 是 | pileId | 某桩评价列表 |
| CLOSE_ACCOUNT | 是 | | 注销留档，不删历史 |
| HEARTBEAT | 是 | | 保活 |

## 钱包版本2示例

```json
{
  "protocolVersion": 2,
  "type": "RECHARGE",
  "seq": 21,
  "token": "session-token",
  "data": {
    "amountCents": 2000,
    "requestId": "9dc97269-7758-4de6-9e6d-e2c682913a74"
  }
}
```

成功时返回 `requestId`、`tradeNo`、`amountCents`、服务器权威 `balanceCents`、`status: succeeded` 和 `replayed`。`QUERY_RECHARGE` 只带原 `requestId`；`QUERY_WALLET` 返回 `balanceCents` 与 `records`。`LIST_RECHARGE` 在迁移期作为旧名称保留。

钱包请求的紧凑JSON最大4 KiB。错误日志只记录 `errorCode` 和已校验的 `requestId`，不记录token或手机号。稳定错误码：`INVALID_AMOUNT`、`INVALID_REQUEST_ID`、`USER_NOT_FOUND`、`USER_FROZEN`、`RECHARGE_NOT_FOUND`、`STORAGE_ERROR`、`AUTH_REQUIRED`、`PROTOCOL_ERROR`。

大屏 HTTP（只读，不是 Socket）：

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 大屏页面 |
| GET | `/api/dashboard` | 版本化完整运营快照（新页面唯一数据源） |
| GET | `/api/overview` | 营收、桩状态、趋势 |
| GET | `/api/analysis` | 预测、风险、调度建议 |
