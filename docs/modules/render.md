# render：场景与地图绘制

`render.c`保留相机路径、表面绘制与帧编排；地图由`domain/render/render_map.c`负责。
`domain/render/map_markers.c`按地图格子统一收集设施标记、已探索过滤、回声去重、同格合成和装置已用状态。
图例与地图共用私有契约`domain/render/map_markers.h`中的语义图标，不另建一套符号。
标记尺寸不得超过地图格子；不在绘制时修改实体、探索状态或回声计时。
本次仅改源码，未编译或运行验证。
