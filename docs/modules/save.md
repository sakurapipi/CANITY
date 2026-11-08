# save（存档：格式、迁移、RNG 版本）

## 职责
把一局的全部状态落盘并读回来：`SaveBlob` 布局、CSAV 信封、版本迁移、
`keepPendingRun`（回主菜单存盘不吃掉进行中的对局）。

## 契约
`include/save.h` —— `SAVE_VERSION`、`SaveBlob`、`SaveGame/LoadSettings`、
`SaveTestBlob/SaveTestPayloadLen`（测试用只读）。

## 绝不做
- **不在中间插字段**。新增字段一律追加到 `SaveBlob` 末尾（AD-57）：
  老档读进来自然是 0，不需要重排布局。
- 追加字段后必须同时做三件事，缺一个就会拒档：
  1. `SAVE_VERSION` +1
  2. 加 `<旧版本> -> 放行` 分支（只改版本号，不动字段）
  3. 检查 `keepPendingRun` 的区段表要不要扩（v35 就因为漏了这条，
     回主菜单存盘会把 rngVersion 清成 0，读档房间重放成另一套）
- 迁移失败绝不能覆盖原件（先留 `.v<旧版本>.bak`）

## 测试
`tests/domains/test_save.c`：5 个历史夹具（v27/v29/v30/v33/v34）必须全部迁移成功；
坏档不被静默覆盖；回菜单存盘保留对局；v33 尾部字段读成 0。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 加一个存档字段 | `SaveBlob` 末尾 + `SAVE_VERSION` +1 + 放行分支 + 区段表 |
| 改迁移逻辑 | `LoadSettings` 里的版本分支 |
| 加一个历史夹具 | `tests/fixtures/saves/` + `test_legacy_fixtures` 的表 |

## 历史坑
- **v29/v30 被拒**：迁移把整份 CSAV 交给按裸 SaveBlob 写的解码器 → 取载荷才对。
- **v34 夹具被拒**：改版本号忘了加放行分支。
- **v33 夹具长度算错**：测试用 `sizeof - N*int` 造老档，加字段后就偏了；
  改成 `offsetof(SaveBlob, echoBolts)` 这种偏移表达才不会错。
- **v35**：`rngVersion` 追加后忘了扩 `runSpans`，回菜单存盘会把它清零。
