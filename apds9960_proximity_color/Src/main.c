/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : APDS-9960 (I2C proximity + RGB color) readout over UART
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - APDS-9960 (Adafruit breakout) on I2C1: PB8 = SCL, PB9 = SDA,
 *   fixed 7-bit address 0x39 (not configurable, unlike most I2C
 *   sensors).
 * - USART2 (PA2=TX, PA3=RX, ST-Link VCP, 115200 8N1) streams the
 *   proximity and RGB/clear color readings.
 *
 * Same I2C1 clock setup as oled_i2c_display/mlx90614_ir_temp: the
 * Timing value 0x00303D5B is only valid with I2C1's kernel clock at
 * 170 MHz, so SystemClock_Config() runs the MCU at 170 MHz.
 *
 * After the MLX90614 turned out to be a dead/incompatible unit, this
 * project checks the sensor's ID register right at boot (and repeats
 * an I2C bus scan periodically) instead of silently assuming the
 * wiring is good -- fail fast and visibly. */

#define APDS9960_ADDR       (0x39 << 1)

#define APDS9960_REG_ENABLE  0x80U
#define APDS9960_REG_ATIME   0x81U
#define APDS9960_REG_WTIME   0x83U
#define APDS9960_REG_PPULSE  0x8EU
#define APDS9960_REG_CONTROL 0x8FU
#define APDS9960_REG_ID      0x92U
#define APDS9960_REG_CDATAL  0x94U
#define APDS9960_REG_PDATA   0x9CU

#define APDS9960_ENABLE_PON  0x01U
#define APDS9960_ENABLE_AEN  0x02U
#define APDS9960_ENABLE_PEN  0x04U

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void uart_send(const char *s);
static void i2c_scan_and_report(void);

static void    apds9960_write_reg(uint8_t reg, uint8_t value);
static uint8_t apds9960_read_reg(uint8_t reg);
static void    apds9960_init(void);
static int     apds9960_read_all(uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b, uint8_t *prox);

int main(void)
{
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();
	MX_I2C1_Init();
	MX_USART2_UART_Init();

	uart_send("APDS-9960 test starting...\r\n");

	for (uint32_t loop_count = 0; ; loop_count++)
	{
		char line[64];

		if ((loop_count % 10U) == 0U)
		{
			i2c_scan_and_report();

			uint8_t id = apds9960_read_reg(APDS9960_REG_ID);
			snprintf(line, sizeof(line), "APDS-9960 ID register: 0x%02X (expected 0xAB)\r\n", id);
			uart_send(line);

			apds9960_init();
		}

		uint16_t c, r, g, b;
		uint8_t  prox;

		if (apds9960_read_all(&c, &r, &g, &b, &prox) == 0)
		{
			snprintf(line, sizeof(line), "Prox: %3u   C: %5u  R: %5u  G: %5u  B: %5u\r\n",
			         prox, c, r, g, b);
		}
		else
		{
			snprintf(line, sizeof(line), "APDS-9960 read error - check wiring/address\r\n");
		}
		uart_send(line);

		HAL_Delay(300);
	}
}

static void apds9960_write_reg(uint8_t reg, uint8_t value)
{
	HAL_I2C_Mem_Write(&hi2c1, APDS9960_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
}

static uint8_t apds9960_read_reg(uint8_t reg)
{
	uint8_t value = 0;

	HAL_I2C_Mem_Read(&hi2c1, APDS9960_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);

	return value;
}

/* Power on, enable ALS (color) + proximity engines. Re-run every few
 * seconds from main() in case the sensor missed its initial
 * power-up window or got reset by a glitch. */
static void apds9960_init(void)
{
	apds9960_write_reg(APDS9960_REG_ENABLE, 0x00);   /* everything off first */
	apds9960_write_reg(APDS9960_REG_ATIME, 0xDB);    /* ~103 ms ALS integration time */
	apds9960_write_reg(APDS9960_REG_WTIME, 0xFF);    /* wait time (unused, WEN stays off) */
	apds9960_write_reg(APDS9960_REG_PPULSE, 0x87);   /* 8 proximity pulses, 16us each */
	apds9960_write_reg(APDS9960_REG_CONTROL, 0x09);  /* LED 100mA, PGAIN 4x, AGAIN 4x */

	apds9960_write_reg(APDS9960_REG_ENABLE, APDS9960_ENABLE_PON);
	HAL_Delay(10); /* let the internal oscillator start up before enabling the engines */
	apds9960_write_reg(APDS9960_REG_ENABLE,
	                    APDS9960_ENABLE_PON | APDS9960_ENABLE_AEN | APDS9960_ENABLE_PEN);
}

/* Reads the 8-byte color block (clear/red/green/blue, little-endian
 * 16-bit each) plus the 1-byte proximity register. Returns 0 on
 * success, -1 on I2C error. */
static int apds9960_read_all(uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b, uint8_t *prox)
{
	uint8_t buf[8];

	if (HAL_I2C_Mem_Read(&hi2c1, APDS9960_ADDR, APDS9960_REG_CDATAL, I2C_MEMADD_SIZE_8BIT, buf, 8, 100) != HAL_OK)
	{
		return -1;
	}

	*c = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
	*r = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
	*g = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
	*b = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);

	if (HAL_I2C_Mem_Read(&hi2c1, APDS9960_ADDR, APDS9960_REG_PDATA, I2C_MEMADD_SIZE_8BIT, prox, 1, 100) != HAL_OK)
	{
		return -1;
	}

	return 0;
}

/* Scans every possible 7-bit I2C address and reports which ones ACK
 * -- learned the hard way with MLX90614 that this is worth doing
 * before trusting any sensor's own protocol. */
static void i2c_scan_and_report(void)
{
	char line[32];

	uart_send("I2C scan starting...\r\n");

	for (uint8_t addr7 = 1U; addr7 < 0x78U; addr7++)
	{
		if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr7 << 1), 2, 10) == HAL_OK)
		{
			snprintf(line, sizeof(line), "  found device at 0x%02X\r\n", addr7);
			uart_send(line);
		}
	}

	uart_send("I2C scan done.\r\n");
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* PB8/PB9: I2C1 SCL/SDA, open-drain + pull-up, AF4 */
	GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
	GPIO_InitStruct.Pull      = GPIO_PULLUP;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* PA2/PA3: USART2 TX/RX, alternate function AF7 */
	GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_I2C1_Init(void)
{
	RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

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

/* HSI (16 MHz) -> PLL -> 170 MHz SYSCLK -- identical to
 * oled_i2c_display/mlx90614_ir_temp, needed because the I2C1 Timing
 * value above is only valid at this exact clock. */
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

void SysTick_Handler(void)
{
	HAL_IncTick();
}
