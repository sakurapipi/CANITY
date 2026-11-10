# 存档夹具（历史版本真实产物）

这些 `.sav` 不是伪造的，也不是从新结构体截断出来的 ——
它们是**各个旧 tag 自己的 `SaveGame()` 写出来的真实文件**。

生成方式（可复现）：

```
git worktree add ../canity-fixtures/<tag> <tag> --detach
# 用 tests/fixtures/gen_fixture_main.c 顶替 src/main.c 做入口，
# 设置最小状态后调用该版本的 SaveGame()
gcc <该 tag 的 src/*.c，去掉 main.c 与 *_web.c> \
    ../canity-fixtures/gen_fixture_main.c \
    -I<该 tag>/include -std=c99 -mconsole \
    -lgdi32 -luser32 -lwinmm -o gen.exe
./gen.exe sav_<tag>.sav
```

| 来源 tag | 文件 | 存档版本 | 格式 | 字节 | SHA256 |
|---|---|---|---|---|---|
| `v2.3.6` | `sav_v2_3_6.sav` | 27 | raw | 6404 | `2692d2f11491d85e26390f56aea5fc440df8af5f3f957019b42692c13c1b778a` |
| `v2.3.9` | `sav_v2_3_9.sav` | 29 | CSAV | 6520 | `b9086549c1603efde02c010b9e76ad4d00cc08cb371750e44f5c55dfd1daa779` |
| `v2.4-rc` | `sav_v2_4-rc.sav` | 30 | CSAV | 6528 | `6e2df26952b836e459d4da6de8bfb81775c8c4252fd56b8b1dcfdf6059fb0db5` |
| `v2.4` | `sav_v2_4.sav` | 33 | CSAV | 12452 | `c40afa9ea1792bf97993d2e5dcad0e674b1821bf49abaffce8765183f6218e88` |
| `v2.4.1` | `sav_v2_4_1.sav` | 34 | CSAV | 12468 | `762ea93355ad3241487ae446c7a757892a7796b01deec9a74c3ed0e0844f9eba` |

## 为什么这组夹具重要

5 个真实存档里 **4 个是 CSAV 信封**（v2.3.9 起）。
而 `src/save.c` 的旧档迁移路径是：

```c
long rn = ReadWholeFile(usedPath, raw, sizeof(SaveBlob) + 4096);
migrated = LoadLegacyBlob(raw, (size_t)rn, legacyCont, ...);
```

它把**整份文件字节**当旧结构体解释 —— 对信封档来说，
前 N 个字节是 `CSAV` 头与块目录，不是 `SaveBlob` 的前缀。
所以「旧档迁移错位」不是理论风险，是**默认路径**。

## 证据等级

- 来源：旧 tag 的真实序列化器 → **A 级**（不是构造/截断）
- 内容：最小状态（有对局标记，地图/容器为默认值）
  → 用于验证**布局与偏移**，不用于验证具体玩法数值

## v35–v44 追加布局契约夹具（本轮新增）

`synthetic_v35.sav` 至 `synthetic_v44.sav` 是明确标记的构造数据，来自 `gen_prefix_contracts.c`，不是历史版本真实序列化器输出，不属于上表的A等级历史证据。固定二进制文件和哈希见 `synthetic-manifest.json`，测试运行时不动态截断当前结构体。包含手动显示设置、音乐音量与有效地图，覆盖读取、默认尾部、原件备份、再次保存和重新读取。

它们填补自动化迁移边界覆盖，不证明所有历史存档变体均已验收。v37早期曾扩展流派数组，真实历史变体仍需独立旧版本存档补证。现有五份真实旧tag夹具保持原样。
