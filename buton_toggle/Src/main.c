/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Button-press-toggled LED using the STM32 HAL
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"

/* NUCLEO-G474RE: user LED LD2 is wired to PA5, user button B1 is
 * wired to PC13. On this board B1 pulls PC13 HIGH when pressed, so
 * an internal pull-down keeps it LOW (released) at rest. */

static void MX_GPIO_Init(void);
static uint32_t button_is_pressed(void);

int main(void)
{
	uint32_t was_pressed = 0;

	HAL_Init();
	MX_GPIO_Init();

	for (;;)
	{
		uint32_t pressed_now = button_is_pressed();

		if (pressed_now && !was_pressed)
		{
			/* Rising edge detected: wait out mechanical bounce, then
			 * confirm the button is still down before acting on it. */
			HAL_Delay(50);

			if (button_is_pressed())
			{
				HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
			}
		}

		was_pressed = pressed_now;
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();

	GPIO_InitStruct.Pin   = GPIO_PIN_5;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	GPIO_InitStruct.Pin  = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static uint32_t button_is_pressed(void)
{
	return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET;
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}
