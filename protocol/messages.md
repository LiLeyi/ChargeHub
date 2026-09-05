# ChargeHub Socket 报文（用户端 ↔ 管理端）

传输：TCP。管理端监听 **所有网卡的 8888 端口**（`0.0.0.0:8888`）。  
用户端在登录页填写 `IP:8888`。同一台电脑填 `127.0.0.1:8888`，连组里服务器填那台电脑的局域网 IP。

帧：`4 字节大端长度 + UTF-8 JSON`（见 `common/protocol.*`）。

管理端 GUI **不走** 这套报文，同进程直接调 `Dispatch`。所以 **不能** 再开第二个管理端去「远程操作」同一台服务器——第二份管理端会自己再开一套库、再抢 8888。

## 请求

```json
{ "type": "LOGIN", "seq": 1, "role": "user", "token": "", "data": {} }
```

## 响应

```json
{ "type": "LOGIN", "seq": 1, "code": 0, "message": "登录成功", "data": {} }
```

`code==0` 成功。除 LOGIN/REGISTER 外，必须带登录返回的 `token`。

常见 `code`：`0` 成功，`400` 参数错，`401` 未登录/密码错，`403` 冻结或已注销，`404` 找不到，`409` 冲突（重复注册、有进行中订单不能注销）。

## type 一览

| type | 登录后 | data 主要字段 | 说明 |
|------|--------|---------------|------|
| LOGIN | 否 | phone, password | 成功返回 user + token |
| REGISTER | 否 | phone, password | 已注销手机号 409 |
| UPDATE_PROFILE | 是 | nickname / avatarBase64 / clearAvatar / address | 改资料；住址会解析坐标 |
| RECHARGE | 是 | amount | 模拟充值 |
| QUERY_STATIONS | 是 | address, radiusKm, lat, lng | 附近电站 + nearbyPiles |
| QUERY_PILES | 是 | stationId | 站内电桩 |
| START_CHARGE | 是 | pileId | 开始充电 |
| CHARGE_STATUS | 是 | | 进行中/待结算实时计费 |
| STOP_CHARGE | 是 | | 结束 → 待结算，桩改闲置 |
| SETTLE_ORDER | 是 | | 扣余额 |
| LIST_ORDERS | 是 | | 我的订单 |
| LIST_RECHARGE | 是 | | 充值记录 |
| LIST_RESERVATIONS | 是 | | 当前用户尚未到期的预约 |
| RESERVE_PILE | 是 | pileId | 预约 15 分钟 |
| CANCEL_RESERVE | 是 | | 取消预约 |
| REVIEW_STATION | 是 | stationId, pileId, score, comment | 评价 |
| LIST_PILE_REVIEWS | 是 | pileId | 某桩评价列表 |
| CLOSE_ACCOUNT | 是 | | 注销留档，不删历史 |
| HEARTBEAT | 是 | | 保活 |

大屏 HTTP（只读，不是 Socket）：

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 大屏页面 |
| GET | `/api/overview` | 营收、桩状态、趋势 |
| GET | `/api/analysis` | 预测、风险、调度建议 |
