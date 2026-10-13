#ifndef __KEY_H
#define __KEY_H

#include <stdint.h>

uint8_t Key_GetNum(void);     // 获取按键值（非阻塞，读取后清零）
void Key_Tick(void);          // 每 1ms 调用一次的扫描函数（放在定时器中断回调中）

#endif
