/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Button-press-toggled LED using a real EXTI interrupt
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"

/* NUCLEO-G474RE: user LED LD2 is wired to PA5, user button B1 is
 * wired to PC13. On this board B1 pulls PC13 HIGH when pressed, so
 * an internal pull-down keeps it LOW (released) at rest.
 *
 * Unlike buton_toggle and oled_i2c_display (which poll PC13 with
 * HAL_GPIO_ReadPin() inside the main loop), this project reacts to a
 * real hardware interrupt: PC13 is configured for
 * GPIO_MODE_IT_RISING, so pressing the button fires the EXTI15_10
 * interrupt and the LED is toggled directly from
 * HAL_GPIO_EXTI_Callback() -- main() never touches the button pin at
 * all, it just sits in an empty loop. */

#define DEBOUNCE_MS 200U

static void MX_GPIO_Init(void);

int main(void)
{
	HAL_Init();
	MX_GPIO_Init();

	/* Nothing to do here: the LED is toggled entirely from the EXTI
	 * interrupt below. */
	for (;;)
	{
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();

	/* PA5: LD2 */
	GPIO_InitStruct.Pin   = GPIO_PIN_5;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* PC13: B1 user button. GPIO_MODE_IT_RISING arms an EXTI interrupt
	 * on the rising edge (this board pulls the line HIGH when
	 * pressed); GPIO_PULLDOWN keeps it a clean LOW at rest so no
	 * spurious edge fires on its own. */
	GPIO_InitStruct.Pin  = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	/* PC13 sits on EXTI line 13, which shares the EXTI15_10 vector
	 * with every other pin 10-15 on any port. */
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* Vector table entry for EXTI lines 10-15. HAL_GPIO_EXTI_IRQHandler()
 * checks which line actually fired, clears its pending bit, and
 * calls HAL_GPIO_EXTI_Callback() below. */
void EXTI15_10_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}

/* Weak in the HAL, overridden here. Called directly from the ISR --
 * this runs every time PC13 rises, so a mechanical bounce right
 * after the button closes could fire it several times in a row. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	static uint32_t last_toggle = 0;

	if (GPIO_Pin == GPIO_PIN_13)
	{
		uint32_t now = HAL_GetTick();

		/* Debounce: ignore any edge that arrives within DEBOUNCE_MS
		 * of the last one we accepted. */
		if ((now - last_toggle) >= DEBOUNCE_MS)
		{
			HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
			last_toggle = now;
		}
	}
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}
