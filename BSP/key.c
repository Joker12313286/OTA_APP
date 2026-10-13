#include "Key.h"
#include "stm32f1xx_hal.h"   // HAL 库主头文件（根据你的芯片型号可能不同，如 stm32f4xx_hal.h）
#include "gpio.h"

static uint8_t Key_Num = 0;   // 保存有效按键值（1 , 2, 3或 4 ）


// 获取当前按键物理状态（0=无按下，1=PB1按下，2=PB11按下）
static uint8_t Key_GetState(void)
{
    if (HAL_GPIO_ReadPin(GPIOB, Key1_Pin) == GPIO_PIN_RESET)
        return 1;
    if (HAL_GPIO_ReadPin(GPIOB, Key2_Pin) == GPIO_PIN_RESET)
        return 2;
		if (HAL_GPIO_ReadPin(GPIOA,	Key3_Pin) == GPIO_PIN_RESET)
				return 3;
		if (HAL_GPIO_ReadPin(GPIOA,	Key4_Pin) == GPIO_PIN_RESET)
				return 4;
    return 0;
}

// 外部调用：获取已消抖的按键值（读取后自动清零）
uint8_t Key_GetNum(void)
{
    uint8_t temp = Key_Num;
    Key_Num = 0;
    return temp;
}

// 定时器中断中每 1ms 调用一次（由 HAL_TIM_PeriodElapsedCallback 触发）
void Key_Tick(void)
{
    static uint8_t count = 0;
    static uint8_t currState = 0, prevState = 0;

    count++;
    if (count >= 20)          // 20ms 消抖周期
    {
        count = 0;
        prevState = currState;
        currState = Key_GetState();

        // 下降沿检测：之前按下，现在释放 → 产生有效按键事件
        if (currState == 0 && prevState != 0)
        {
            Key_Num = prevState;
        }
    }
}