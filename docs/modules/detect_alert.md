# detect_alert（被发现预警）

## 职责
怪从"没发现你"切到"开始追"的那一帧，给一声有方向的短音 +
一句「它盯上你了 —— 断视线，或者躲进柜子」。

## 为什么
玩家反馈「要怎么躲」「我每次都硬吃」。
只有守门人有 1.5 秒锁定预警（`warden.spotT`），别的怪从察觉到开追是**瞬间**的，
玩家反应过来已经在挨咬。

## 契约
`include/detect_alert.h`：`DetectAlertTick(dt)` / `DetectAlertAge()` / `DetectAlertReset()`
判据：上一帧 `state == 0` 且这一帧 `state != 0`。

## 绝不做
- **不改 AI**：发现的事实不改变，只让它可读
- 不每帧重复报：持续在追不算新发现（有测试）

## 测试
`tests/domains/test_actors.c`：0→2 恰好报一次、持续在追不重复、时钟归零。

## 历史坑
- `pickupMsg` / `pickupMsgTimer` 在 `game.h`，不要在函数里 extern
- `g_lang` / `LANG_ZH` 在 `lang.h`
