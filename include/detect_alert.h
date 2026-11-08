/* include/detect_alert.h —— 被发现预警（第 4 轮，2026-11-08）
 *
 * 玩家反馈：「要怎么躲」「我每次都硬吃」。
 * 原因：只有守门人有锁定预警，别的怪从"没发现"到"开追"是瞬间的，
 * 玩家来不及反应，只能挨咬。
 *
 * 契约：**只读 AI 状态**。发现的事实不改变，只让它可读。
 */
#ifndef CANITY_DETECT_ALERT_H
#define CANITY_DETECT_ALERT_H

/* 每帧调用（在怪物 AI 之后）。返回本条帧新增的"刚发现你"的怪数量。 */
int DetectAlertTick(float dt);

/* 距离上次被发现过了多久（秒）；很大 = 很久没被发现 */
float DetectAlertAge(void);

/* 测试用 */
void DetectAlertReset(void);

#endif
