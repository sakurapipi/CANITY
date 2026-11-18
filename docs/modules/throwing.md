# 投掷物

- `src/throwing.c` 负责生成投掷物和逐步推进飞行。
- `src/domain/throwing/shot_impact.c` 负责碰撞目标、破锁和落点结算。地面、墙面、怪物和寿命到期都经过 `FinishShot`。
- `src/content/content_shots.c` 登记投掷物规格、噪音和效果类型。
- `src/domain/throwing/decoy_runtime.c` 管理玩偶放置、启动、叫声和摧毁。
- `src/domain/throwing/flare_heal.c` 管理荧光棒治疗窗口和楼层预算。

新增投掷效果时，先在规格表声明类型，再实现对应的命中效果。飞行模块通过 `throwing_runtime.h` 调用命中模块，场景通过 `throwing.h` 访问投掷系统。

回归用例位于 `tests/domains/test_throwing.c`，包括撞墙落点、单次破锁噪音、直接命中生成一份胶水，以及荧光棒治疗预算。
