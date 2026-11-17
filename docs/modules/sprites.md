# sprites：程序化精灵

## 所有者

- `src/sprites.c`：精灵存储、初始化编排、扳手烘焙和运行时采样绘制。
- `src/domain/sprites/sprite_canvas.c`：唯一超采样画布、画布比例、烘焙随机源、
  图元及降采样。画布只用于启动烘焙，不是每帧状态。
- `sprite_actors.c`：怪物形体、材质、姿态和专属上下文。
- `sprite_props.c`：道具、舱门、箱子、房间家具及 UI 贴片。

## 契约

玩家侧 API 继续使用 `include/sprites.h`；内部烘焙操作通过同目录
`sprite_bake.h` 协作。`SpriteBakeTarget` 只在核心分配完成后的启动序列中使用，
不受“初始化完成”标志影响；游戏绘制仍通过 `SpriteGet` 查询。
精灵 ID、烘焙调用顺序、逻辑画布尺寸和现有怪物造型保持原定义。
禁止在怪物或物件模块再定义一套精灵数组、随机种子或画布。
2.4.8-RC 的代码搬迁尚未编译或验证。

RC2新增 `sprite_theme_props.c`：按楼层材质烘焙七类隐藏点、箱子和配电箱；`sprite_room_devices.c`拥有房间装置准备姿态及运行机械帧。全部使用唯一共享画布与精灵存储，启动时生成。
