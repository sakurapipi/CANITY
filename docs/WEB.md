# CANITY 网页版

同一份 C99 源码，通过 Emscripten 编成 WebAssembly 在浏览器里跑。
**没有重写任何游戏逻辑或渲染算法** —— 换的只是一层平台接口。

---

## 快速开始

```bash
# 1. 构建（需要 emsdk）
python -X utf8 tools/build_web.py

# 2. 本地预览
cd web/dist && python -m http.server 8080
# 打开 http://127.0.0.1:8080/
```

产物在 `web/dist/`：

```
index.html     2.5 KB   外壳（canvas + 加载界面 + 下载入口）
index.js      71   KB   Emscripten 胶水
index.wasm  1205  KB   游戏本体
```

**必须用 HTTP 打开**，`file://` 不行（WASM 的 fetch 会被 CORS 拦）。

---

## 工具链

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/.dsh/tools/emsdk
cd ~/.dsh/tools/emsdk && emsdk.bat install latest
```

本机路径随机器而异：构建脚本默认读 `~/.dsh/tools/emsdk`，可用环境变量 `CANITY_EMSDK` 覆盖。
配置：`upstream/emscripten/.emscripten`（`build_web.py` 会自动指过去）

> **Windows 上的坑**：`emsdk.bat install` 解压会在
> `emscripten/test/test_other.py` 上报 WinError 5。
> 解压出来的东西是完整的，只是路径多了一层 `install/`。
> 修法：把 `upstream/install/emscripten/` 覆盖到 `upstream/emscripten/`（robocopy /E）。
> 本机已经处理过，`emcc --version` 能打印 6.0.13 就算好了。

---

## 架构

```
include/plat.h                唯一跨平台接口（无任何 Win32 类型）
include/common.h              系统头唯一入口（网页端在这里切到 emscripten.h + 垫片）
src/platform/plat_win32.c     桌面后端（DIB + BitBlt + GetAsyncKeyState + 文件系统）
src/platform/plat_web.c       网页后端（canvas + localStorage）
src/main.c                    Win32 入口（WinMain + 消息循环）
src/platform/main_web.c       网页入口（main + rAF 主循环）
src/audio_web.c               音频分区的网页后端
src/music_web.c               配乐分区的网页后端
src/gamepad_web.c             手柄分区的网页后端
src/steam/steam_zone_web.c    Steam 分区的网页后端
```

### 分区规则（tools/partition_check.py 逐条检查）

```
S1  除 common.h 外，任何 .c/.h 都不直接包含系统头
S2  只有 src/save/save_backend.c 能读写 .sav
S3  每个分区的公开符号只能由该分区自己的实现文件定义
```

**S3 是这轮补的**：网页移植的第一版把 audio / music / gamepad / steam 的桩
全塞进了 `plat_web.c`，平台层变成"什么都懂一点"的杂物间。
现在桩回到各自分区，平台层只管平台，规则会一直盯着。

Win32 的类型与键码垫片**写在 `common.h` 里**（`#ifdef CANITY_WEB` 分支），
而不是单开一个 `compat/web_compat.h` —— 一旦允许第二个文件包含系统头，
"唯一入口"这条约定就名存实亡。

### 数据流

```
游戏写 pixels[]
   ├── 桌面：pixels 建在 DIB Section 上 → StretchBlt
   └── 网页：pixels 是普通内存 → ImageData → putImageData
```

文字不需要额外处理：`ui.c` 的 `DrawUiTextRaw` 本来就是**软件点阵字**
（8×12 ASCII 表 + 12×12 中文图集），直接写 `pixels[]`。

---

## 平台接口一览

| 组 | 函数 |
|---|---|
| 时间 | `PlatNow` `PlatSleepMs` |
| 窗口 | `PlatWindowSize` `PlatMinimized` `PlatFocused` `PlatRestoreWindow` `PlatSetTitle` `PlatRequestQuit` |
| 呈现 | `PlatAcquireTarget` `PlatPresentFrame` `PlatShutdown` `PlatTargetPtr` |
| 输入 | `PlatKeyDown` `PlatMouseDelta` `PlatSetCursorLock` `PlatCursorLocked` `PlatCursorPos` `PlatSetCursorPos` `PlatShowCursor` `PlatSetCapture` |
| 文件 | `PlatReadFilePath` `PlatWriteFileAtomic` `PlatCopyPath` `PlatRenamePath` `PlatDeletePath` `PlatFileTime` `PlatMakeDir` `PlatExeDir` |
| 其它 | `PlatSfx` `PlatLog` `PlatSystemLanguageZh` |

---

## 平台差异

| 项 | 桌面 | 网页 |
|---|---|---|
| 渲染线程 | 主线程 + 最多 8 个 worker 分带 | **单线程**（WASM 默认无 pthread） |
| 存档 | `canityN.sav`（原子写 + `.bak` 链） | `localStorage['canity:canityN.sav']`（base64） |
| 音频 | waveOut 合成 | **静音**（`audio.c` 在网页构建里被排除） |
| 配乐 | MCI | **静音**（`music.c` 被排除） |
| 手柄 | XInput 动态加载 | 无（`gamepad.c` 被排除） |
| Steam | Steamworks | 全部空实现（`steam_zone.c` 被排除） |
| 入口 | `src/main.c`（WinMain） | `src/platform/main_web.c`（rAF） |
| 全屏 | F11 / Alt+Enter | 浏览器全屏（由外壳处理） |

被排除的五个 `.c` 都有同名桩函数在 `plat_web.c` 里，
所以**不会因为缺符号链接失败**，只是对应功能不生效。

---

## 触屏

- 左侧虚拟摇杆：移动（映射到 W/A/S/D）
- 右侧半屏拖动：转视角
- 右下按钮：交互 E / 扳手 V / 挣脱·跳跃 空格 / 地图 M / 投掷（右键）
- 桌面调试时加 `?touch=1` 可以强制显示触控层

---

## 验证

```bash
# 桌面回归（每次改完都必须绿）
python -X utf8 tools/build_validate.py     # 期望 ALL VALIDATION PASSED

# 网页存档往返自检
#   打开 http://127.0.0.1:8080/?selftest=1
#   页面大标题会变成 savetest-ok / savetest-fail
```

---

## 已知限制

1. **性能**：单线程软件渲染。内部渲染分辨率有像素预算上限（约 28 万像素），
   1080p 下会降到 640×360 再放大 —— 和桌面版的动态分辨率是同一套逻辑。
2. **音频**：一期静音。R7 计划接 WebAudio（浏览器自动播放策略要求
   首次用户手势后才 `resume()`）。
3. **存档**：只按文件名做键，多存档槽之间不会串，但**不同目录的同名文件会互相覆盖**
   （网页端本来就没有目录）。
4. **触屏手感**：第一人称 + 0.42 秒 QTE 在手机上偏难，后续会放宽 QTE 窗口。
5. **历史原型债务**：这轮清掉了 141 处「头文件里已有、.c 里又声明一遍」的重复
   原型；还剩 `entity.c` / `render.c` / `main_web.c` 三个文件因为删了会破坏
   编译顺序，暂未清理。
