/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : PC <-> STM32 command interface over UART (USART2)
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* NUCLEO-G474RE: user LED LD2 is wired to PA5. USART2 (PA2=TX, PA3=RX)
 * is the UART routed through the on-board ST-Link Virtual COM Port, so
 * it shows up as a normal serial port on the PC (115200 8N1). */

#define RX_LINE_MAX 64

static UART_HandleTypeDef huart2;

static uint8_t  rx_byte;
static char     rx_line[RX_LINE_MAX];
static volatile uint16_t rx_index = 0;
static volatile uint8_t  line_ready = 0;

static uint8_t led_is_on = 0;

static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void uart_send(const char *s);
static void process_command(const char *cmd);

int main(void)
{
	HAL_Init();
	MX_GPIO_Init();
	MX_USART2_UART_Init();

	uart_send("Ready. Commands: LED ON / LED OFF / STATUS\r\n");
	HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

	for (;;)
	{
		if (line_ready)
		{
			process_command(rx_line);
			rx_index = 0;
			line_ready = 0;
		}
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();

	/* LD2 */
	GPIO_InitStruct.Pin   = GPIO_PIN_5;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* USART2: PA2 = TX, PA3 = RX, alternate function AF7 */
	GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_USART2_UART_Init(void)
{
	__HAL_RCC_USART2_CLK_ENABLE();

	huart2.Instance                    = USART2;
	huart2.Init.BaudRate               = 115200;
	huart2.Init.WordLength             = UART_WORDLENGTH_8B;
	huart2.Init.StopBits               = UART_STOPBITS_1;
	huart2.Init.Parity                 = UART_PARITY_NONE;
	huart2.Init.Mode                   = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
	huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
	huart2.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
	huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	HAL_UART_Init(&huart2);

	HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2);
}

/* Called from the ISR each time one byte has been received. Bytes are
 * accumulated into rx_line until '\r' or '\n' terminates the command,
 * at which point main()'s loop is signalled via line_ready. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		char c = (char)rx_byte;

		if (c == '\r' || c == '\n')
		{
			if (rx_index > 0)
			{
				rx_line[rx_index] = '\0';
				line_ready = 1;
			}
		}
		else if (rx_index < (RX_LINE_MAX - 1))
		{
			rx_line[rx_index++] = c;
		}

		/* Re-arm reception for the next byte. */
		HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
	}
}

static void uart_send(const char *s)
{
	HAL_UART_Transmit(&huart2, (const uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

static void process_command(const char *cmd)
{
	if (strcmp(cmd, "LED ON") == 0)
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
		led_is_on = 1;
		uart_send("OK\r\n");
	}
	else if (strcmp(cmd, "LED OFF") == 0)
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
		led_is_on = 0;
		uart_send("OK\r\n");
	}
	else if (strcmp(cmd, "STATUS") == 0)
	{
		char reply[80];

		/* Temperature/ADC are placeholders here: real sensor readings
		 * belong to the upcoming ADC lesson, this project's focus is
		 * UART + interrupt + parsing. */
		snprintf(reply, sizeof(reply),
		         "LED: %s\r\nTemperature: 24.5 C\r\nADC: 1823\r\n",
		         led_is_on ? "ON" : "OFF");
		uart_send(reply);
	}
	else
	{
		uart_send("ERR: unknown command\r\n");
	}
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}
