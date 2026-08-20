/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : DS18B20 (1-Wire) temperature readout, streamed over UART
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - DS18B20 DATA -> PA0, with a 4.7k ohm pull-up resistor from PA0 to
 *   3V3 (required: 1-Wire is open-drain and idles high through the
 *   pull-up). DS18B20 VDD -> 3V3, GND -> GND.
 * - USART2 (PA2=TX, PA3=RX, ST-Link VCP, 115200 8N1) streams the
 *   temperature so it can be read from a PC terminal.
 *
 * There is no HAL peripheral for 1-Wire, so the bus is bit-banged by
 * hand: PA0 stays configured as Output Open-Drain the whole time.
 * Driving it low pulls the bus low; setting it "high" just releases
 * the pin so the external pull-up brings it back up (classic
 * open-drain trick). Reading the pin's IDR value works in either
 * state, so the same pin doubles as input without ever touching
 * GPIO_MODER. All the 1-Wire time slots (reset/write/read) are
 * timed with the Cortex-M4 DWT cycle counter for microsecond
 * precision -- no timer peripheral needed for that either. */

#define ONEWIRE_PORT   GPIOA
#define ONEWIRE_PIN    GPIO_PIN_0

#define DS18B20_CMD_SKIP_ROM        0xCCU
#define DS18B20_CMD_CONVERT_T       0x44U
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEU

static UART_HandleTypeDef huart2;

static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void DWT_Init(void);
static void delay_us(uint32_t us);

static void OW_Low(void);
static void OW_Release(void);
static uint8_t OW_Read(void);
static uint8_t OW_Reset(void);
static void OW_WriteBit(uint8_t bit);
static uint8_t OW_ReadBit(void);
static void OW_WriteByte(uint8_t byte);
static uint8_t OW_ReadByte(void);

static void uart_send(const char *s);

int main(void)
{
	HAL_Init();
	DWT_Init();
	MX_GPIO_Init();
	MX_USART2_UART_Init();

	uart_send("DS18B20 test starting...\r\n");

	for (;;)
	{
		if (!OW_Reset())
		{
			uart_send("DS18B20 not found (no presence pulse) - check wiring/pull-up\r\n");
			HAL_Delay(1000);
			continue;
		}

		OW_WriteByte(DS18B20_CMD_SKIP_ROM);
		OW_WriteByte(DS18B20_CMD_CONVERT_T);

		/* 12-bit conversion takes up to 750 ms. */
		HAL_Delay(750);

		OW_Reset();
		OW_WriteByte(DS18B20_CMD_SKIP_ROM);
		OW_WriteByte(DS18B20_CMD_READ_SCRATCHPAD);

		uint8_t lsb = OW_ReadByte();
		uint8_t msb = OW_ReadByte();

		int16_t raw       = (int16_t)((uint16_t)(msb << 8) | lsb);
		int32_t t_tenths  = ((int32_t)raw * 10) / 16; /* 1 LSB = 1/16 C */
		int32_t t_abs     = (t_tenths < 0) ? -t_tenths : t_tenths;

		char line[48];
		snprintf(line, sizeof(line), "Temperature: %s%ld.%ld C\r\n",
		         (t_tenths < 0) ? "-" : "", (long)(t_abs / 10), (long)(t_abs % 10));
		uart_send(line);

		HAL_Delay(250);
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();

	/* PA0: 1-Wire bus, open-drain so "high" just releases the line to
	 * the external pull-up. */
	GPIO_InitStruct.Pin   = ONEWIRE_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(ONEWIRE_PORT, &GPIO_InitStruct);
	OW_Release();

	/* PA2/PA3: USART2 TX/RX, alternate function AF7 */
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

	huart2.Instance            = USART2;
	huart2.Init.BaudRate       = 115200;
	huart2.Init.WordLength     = UART_WORDLENGTH_8B;
	huart2.Init.StopBits       = UART_STOPBITS_1;
	huart2.Init.Parity         = UART_PARITY_NONE;
	huart2.Init.Mode           = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling   = UART_OVERSAMPLING_16;
	huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
	huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	HAL_UART_Init(&huart2);
}

static void uart_send(const char *s)
{
	HAL_UART_Transmit(&huart2, (const uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

/* --- Microsecond delay via the Cortex-M4 DWT cycle counter --- */

static void DWT_Init(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
	uint32_t start   = DWT->CYCCNT;
	uint32_t cycles  = us * (SystemCoreClock / 1000000U);

	while ((DWT->CYCCNT - start) < cycles)
	{
	}
}

/* --- 1-Wire bus primitives (all timings per the DS18B20 datasheet) --- */

static void OW_Low(void)
{
	HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_RESET);
}

static void OW_Release(void)
{
	HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
}

static uint8_t OW_Read(void)
{
	return (HAL_GPIO_ReadPin(ONEWIRE_PORT, ONEWIRE_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

/* Reset pulse + presence detect. Returns 1 if a device answered. */
static uint8_t OW_Reset(void)
{
	uint8_t presence;

	OW_Low();
	delay_us(480);
	OW_Release();
	delay_us(70);
	presence = (OW_Read() == 0U) ? 1U : 0U; /* slave pulls the line low if present */
	delay_us(410);

	return presence;
}

static void OW_WriteBit(uint8_t bit)
{
	OW_Low();
	if (bit)
	{
		delay_us(6);
		OW_Release();
		delay_us(64);
	}
	else
	{
		delay_us(60);
		OW_Release();
		delay_us(10);
	}
}

static uint8_t OW_ReadBit(void)
{
	uint8_t bit;

	OW_Low();
	delay_us(6);
	OW_Release();
	delay_us(9);
	bit = OW_Read();
	delay_us(55);

	return bit;
}

static void OW_WriteByte(uint8_t byte)
{
	for (uint8_t i = 0; i < 8U; i++)
	{
		OW_WriteBit(byte & 0x01U);
		byte >>= 1;
	}
}

static uint8_t OW_ReadByte(void)
{
	uint8_t byte = 0;

	for (uint8_t i = 0; i < 8U; i++)
	{
		byte |= (uint8_t)(OW_ReadBit() << i);
	}

	return byte;
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}
