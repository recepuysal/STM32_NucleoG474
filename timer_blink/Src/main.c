/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Timer-driven LED blink (500 ms) using the STM32 HAL
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"

/* NUCLEO-G474RE: user LED LD2 is wired to PA5.
 * TIM3 is clocked from PCLK1 = 16 MHz (default HSI16, no prescalers touched).
 * Prescaler 15999 -> 16 MHz / 16000 = 1 kHz timer tick (1 ms per tick).
 * Period 499       -> 500 ticks per update event = 500 ms. */

static TIM_HandleTypeDef htim3;

static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);

int main(void)
{
	HAL_Init();

	MX_GPIO_Init();
	MX_TIM3_Init();

	HAL_TIM_Base_Start_IT(&htim3);

	/* Nothing to do here: the LED is toggled entirely from the
	 * TIM3 update interrupt, no polling and no HAL_Delay(). */
	for (;;)
	{
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitStruct.Pin   = GPIO_PIN_5;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_TIM3_Init(void)
{
	__HAL_RCC_TIM3_CLK_ENABLE();

	htim3.Instance               = TIM3;
	htim3.Init.Prescaler         = 15999;
	htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
	htim3.Init.Period            = 499;
	htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	HAL_TIM_Base_Init(&htim3);

	HAL_NVIC_SetPriority(TIM3_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

void TIM3_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim3);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM3)
	{
		HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
	}
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}
