#ifndef CANITY_GAMEPAD_H
#define CANITY_GAMEPAD_H

/* v2.0 手柄（动态加载 xinput，不增加链接依赖） */
void GamepadInit(void);
void GamepadPoll(void);
int  GamepadConnected(void);

#endif
