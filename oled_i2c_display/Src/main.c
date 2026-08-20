/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : SSD1306 OLED (I2C) status display: DS18B20 temp/LED
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - OLED module (SSD1306, 128x64) on I2C1: PB8 = SCL, PB9 = SDA.
 * - DS18B20 (1-Wire, same wiring/bit-banging as ds18b20_temperature)
 *   on PA0, with a 4.7k pull-up to 3V3.
 * - User LED LD2 on PA5, toggled by user button B1 (PC13), same
 *   logic as buton_toggle.
 *
 * I2C1's timing register (and the PLL setup below) are taken as-is
 * from ST's own NUCLEO-G474RE I2C example
 * (STM32CubeG4/Projects/NUCLEO-G474RE/Examples/I2C/I2C_TwoBoards_ComPolling),
 * which runs the MCU at 170 MHz (HSI -> PLL) and feeds I2C1 from
 * SYSCLK. The 0x00303D5B timing value is only valid for that exact
 * clock setup -- unlike every earlier project in this repo, this one
 * cannot stay at the default 16 MHz HSI, so SystemClock_Config() is
 * introduced here for the first time. */

#define ONEWIRE_PORT   GPIOA
#define ONEWIRE_PIN    GPIO_PIN_0

#define DS18B20_CMD_SKIP_ROM        0xCCU
#define DS18B20_CMD_CONVERT_T       0x44U
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEU

#define DS18B20_CONVERSION_MS 750U

/* Not static: ssd1306.h declares "extern I2C_HandleTypeDef hi2c1"
 * (via the SSD1306_I2C_PORT macro in ssd1306_conf.h) and the library
 * reaches into this handle directly from ssd1306.c. */
I2C_HandleTypeDef hi2c1;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static uint32_t button_is_pressed(void);

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

int main(void)
{
	uint32_t was_pressed    = 0;
	uint8_t  led_is_on      = 0;
	uint32_t last_refresh   = 0;
	uint32_t ds_last_action = 0;
	uint8_t  ds_waiting     = 0; /* 0 = about to trigger a conversion, 1 = waiting for it */
	uint8_t  ds_present     = 0;
	int32_t  t_tenths       = 0;

	HAL_Init();
	SystemClock_Config();
	DWT_Init();
	MX_GPIO_Init();
	MX_I2C1_Init();

	ssd1306_Init();
	ssd1306_Fill(Black);
	ssd1306_UpdateScreen();

	for (;;)
	{
		/* Button: press toggles the LED (same edge-detect + debounce
		 * pattern as buton_toggle). */
		uint32_t pressed_now = button_is_pressed();

		if (pressed_now && !was_pressed)
		{
			HAL_Delay(50);
			if (button_is_pressed())
			{
				led_is_on = !led_is_on;
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5,
				                   led_is_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
			}
		}
		was_pressed = pressed_now;

		/* DS18B20: a small non-blocking state machine so the 750 ms
		 * conversion time never stalls button handling or the screen
		 * refresh below (unlike ds18b20_temperature's simpler
		 * HAL_Delay(750), which can get away with it because that
		 * project has nothing else to keep responsive). */
		if (!ds_waiting && (HAL_GetTick() - ds_last_action) >= 250U)
		{
			if (OW_Reset())
			{
				OW_WriteByte(DS18B20_CMD_SKIP_ROM);
				OW_WriteByte(DS18B20_CMD_CONVERT_T);
				ds_present = 1;
				ds_waiting = 1;
			}
			else
			{
				ds_present = 0;
			}
			ds_last_action = HAL_GetTick();
		}
		else if (ds_waiting && (HAL_GetTick() - ds_last_action) >= DS18B20_CONVERSION_MS)
		{
			OW_Reset();
			OW_WriteByte(DS18B20_CMD_SKIP_ROM);
			OW_WriteByte(DS18B20_CMD_READ_SCRATCHPAD);

			uint8_t lsb = OW_ReadByte();
			uint8_t msb = OW_ReadByte();
			int16_t raw = (int16_t)((uint16_t)(msb << 8) | lsb);

			t_tenths   = ((int32_t)raw * 10) / 16; /* 1 LSB = 1/16 C */
			ds_waiting = 0;
			ds_last_action = HAL_GetTick();
		}

		/* Redraw the screen a few times a second -- no need to touch
		 * the I2C bus on every single loop iteration. */
		if ((HAL_GetTick() - last_refresh) >= 300U)
		{
			char line[22];

			ssd1306_Fill(Black);

			ssd1306_SetCursor(0, 0);
			ssd1306_WriteString("STM32 G474RE", Font_6x8, White);

			ssd1306_SetCursor(0, 10);
			ssd1306_WriteString("----------------", Font_6x8, White);

			if (ds_present)
			{
				int32_t t_abs = (t_tenths < 0) ? -t_tenths : t_tenths;

				snprintf(line, sizeof(line), "Temp: %s%ld.%ld C",
				         (t_tenths < 0) ? "-" : "", (long)(t_abs / 10), (long)(t_abs % 10));
			}
			else
			{
				snprintf(line, sizeof(line), "Temp: --.- C");
			}
			ssd1306_SetCursor(0, 22);
			ssd1306_WriteString(line, Font_6x8, White);

			ssd1306_SetCursor(0, 34);
			ssd1306_WriteString(led_is_on ? "LED : ON" : "LED : OFF", Font_6x8, White);

			ssd1306_UpdateScreen();

			last_refresh = HAL_GetTick();
		}
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();

	/* PA0: 1-Wire bus, open-drain so "high" just releases the line to
	 * the external pull-up (same trick as ds18b20_temperature). */
	GPIO_InitStruct.Pin   = ONEWIRE_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(ONEWIRE_PORT, &GPIO_InitStruct);
	OW_Release();

	/* PA5: LD2 */
	GPIO_InitStruct.Pin   = GPIO_PIN_5;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* PC13: B1 user button (pulls HIGH when pressed on this board) */
	GPIO_InitStruct.Pin  = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	/* PB8/PB9: I2C1 SCL/SDA, open-drain + pull-up, AF4 */
	GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
	GPIO_InitStruct.Pull      = GPIO_PULLUP;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void MX_I2C1_Init(void)
{
	RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

	/* I2C1's kernel clock is fed from SYSCLK (170 MHz) so it matches
	 * the Timing value below -- see the comment at the top of the
	 * file. */
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
	PeriphClkInit.I2c1ClockSelection   = RCC_I2C1CLKSOURCE_SYSCLK;
	HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

	__HAL_RCC_I2C1_CLK_ENABLE();

	hi2c1.Instance              = I2C1;
	hi2c1.Init.Timing           = 0x00303D5B; /* from ST's NUCLEO-G474RE I2C example, 170 MHz I2C1 kernel clock */
	hi2c1.Init.OwnAddress1      = 0;
	hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2      = 0;
	hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
	HAL_I2C_Init(&hi2c1);
}

/* HSI (16 MHz) -> PLL -> 170 MHz SYSCLK. Needed because the I2C1
 * timing value above is only valid at this exact clock; every
 * earlier project in this repo stayed at the default 16 MHz HSI and
 * didn't need this function at all. */
static void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

	RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = 64;
	RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV4;
	RCC_OscInitStruct.PLL.PLLN            = 85;
	RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
	RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
	HAL_RCC_OscConfig(&RCC_OscInitStruct);

	RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
	                                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

static uint32_t button_is_pressed(void)
{
	return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET;
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
	uint32_t start  = DWT->CYCCNT;
	uint32_t cycles = us * (SystemCoreClock / 1000000U);

	while ((DWT->CYCCNT - start) < cycles)
	{
	}
}

/* --- 1-Wire bus primitives (all timings per the DS18B20 datasheet,
 * identical to ds18b20_temperature) --- */

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

static uint8_t OW_Reset(void)
{
	uint8_t presence;

	OW_Low();
	delay_us(480);
	OW_Release();
	delay_us(70);
	presence = (OW_Read() == 0U) ? 1U : 0U;
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
