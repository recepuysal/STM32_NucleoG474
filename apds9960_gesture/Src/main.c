/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : APDS-9960 (I2C gesture detection) readout over UART
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - APDS-9960 (Adafruit breakout) on I2C1: PB8 = SCL, PB9 = SDA,
 *   fixed 7-bit address 0x39 (not configurable, unlike most I2C
 *   sensors).
 * - USART2 (PA2=TX, PA3=RX, ST-Link VCP, 115200 8N1) stays silent
 *   otherwise and only prints a line when a gesture is actually
 *   detected -- no continuous proximity/color telemetry.
 *
 * Same I2C1 clock setup as oled_i2c_display/mlx90614_ir_temp: the
 * Timing value 0x00303D5B is only valid with I2C1's kernel clock at
 * 170 MHz, so SystemClock_Config() runs the MCU at 170 MHz.
 *
 * After the MLX90614 turned out to be a dead/incompatible unit, this
 * project checks the sensor's ID register and scans the I2C bus once
 * at boot instead of silently assuming the wiring is good.
 *
 * Gesture detection: the APDS-9960 has four photodiodes (Up/Down/
 * Left/Right) and streams one (U,D,L,R) byte-quadruplet per sample
 * into its GFIFO while an object is within gesture range. This is a
 * *simplified* direction algorithm -- it just compares the first
 * FIFO sample of a gesture against the last one (instead of the
 * weighted multi-sample decision matrix real gesture libraries use),
 * so it's less robust on ambiguous diagonal swipes, but it is a real
 * read of real hardware data, not a canned demo. Treat GPENTH/GEXTH
 * and the UP/DOWN/LEFT/RIGHT sign convention below as a starting
 * point to tune against your own hand speed and lighting. */

#define APDS9960_ADDR       (0x39 << 1)

#define APDS9960_REG_ENABLE  0x80U
#define APDS9960_REG_ATIME   0x81U
#define APDS9960_REG_WTIME   0x83U
#define APDS9960_REG_PPULSE  0x8EU
#define APDS9960_REG_CONTROL 0x8FU
#define APDS9960_REG_ID      0x92U
#define APDS9960_REG_GPENTH  0xA0U
#define APDS9960_REG_GEXTH   0xA1U
#define APDS9960_REG_GCONF1  0xA2U
#define APDS9960_REG_GCONF2  0xA3U
#define APDS9960_REG_GPULSE  0xA6U
#define APDS9960_REG_GCONF3  0xAAU
#define APDS9960_REG_GCONF4  0xABU
#define APDS9960_REG_GFLVL   0xAEU
#define APDS9960_REG_GSTATUS 0xAFU
#define APDS9960_REG_GFIFO_U 0xFCU

#define APDS9960_ENABLE_PON  0x01U
#define APDS9960_ENABLE_AEN  0x02U
#define APDS9960_ENABLE_PEN  0x04U
#define APDS9960_ENABLE_GEN  0x40U

#define APDS9960_GSTATUS_GVALID 0x01U

#define GESTURE_DIFF_THRESHOLD  12  /* tune this if gestures don't trigger or trigger too easily */
#define GESTURE_AXIS_MARGIN_PCT 40  /* winning axis must beat the other by this % to be trusted */

typedef enum
{
	GESTURE_NONE = 0,
	GESTURE_UP,
	GESTURE_DOWN,
	GESTURE_LEFT,
	GESTURE_RIGHT
} gesture_t;

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void uart_send(const char *s);
static void i2c_scan_and_report(void);

static void      apds9960_write_reg(uint8_t reg, uint8_t value);
static uint8_t   apds9960_read_reg(uint8_t reg);
static void      apds9960_init(void);
static gesture_t apds9960_poll_gesture(int32_t *out_diff_ud, int32_t *out_diff_lr);
static const char *gesture_name(gesture_t g);

int main(void)
{
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();
	MX_I2C1_Init();
	MX_USART2_UART_Init();

	uart_send("APDS-9960 gesture test starting...\r\n");
	i2c_scan_and_report();

	char line[64];
	uint8_t id = apds9960_read_reg(APDS9960_REG_ID);
	snprintf(line, sizeof(line), "APDS-9960 ID register: 0x%02X (expected 0xAB)\r\n", id);
	uart_send(line);

	apds9960_init();

	/* Serial stays silent otherwise -- only a completed gesture event
	 * writes anything here, no continuous proximity/color spam. Every
	 * completed event prints its raw UD/LR diffs (not just confidently
	 * classified ones) so GESTURE_DIFF_THRESHOLD/GESTURE_AXIS_MARGIN_PCT
	 * can be tuned against real numbers instead of guessing. */
	for (;;)
	{
		int32_t diff_ud = INT32_MIN;
		int32_t diff_lr = INT32_MIN;
		gesture_t g = apds9960_poll_gesture(&diff_ud, &diff_lr);

		if (diff_ud != INT32_MIN)
		{
			snprintf(line, sizeof(line), "Gesture: %-5s  (diff_ud=%ld diff_lr=%ld)\r\n",
			         gesture_name(g), (long)diff_ud, (long)diff_lr);
			uart_send(line);
		}

		HAL_Delay(50);
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

/* Power on, enable ALS (color) + proximity + gesture engines. */
static void apds9960_init(void)
{
	apds9960_write_reg(APDS9960_REG_ENABLE, 0x00);   /* everything off first */
	apds9960_write_reg(APDS9960_REG_ATIME, 0xDB);    /* ~103 ms ALS integration time */
	apds9960_write_reg(APDS9960_REG_WTIME, 0xFF);    /* wait time (unused, WEN stays off) */
	apds9960_write_reg(APDS9960_REG_PPULSE, 0x87);   /* 8 proximity pulses, 16us each */
	apds9960_write_reg(APDS9960_REG_CONTROL, 0x09);  /* LED 100mA, PGAIN 4x, AGAIN 4x */

	/* Gesture engine configuration (typical starting values, tune
	 * GPENTH/GEXTH if it doesn't trigger reliably at your hand's
	 * distance/speed): enter when proximity > 40, exit when < 30,
	 * FIFO threshold 4 samples, 2x gesture gain / 100 mA LED, 32us
	 * pulses x10, all four photodiodes (U/D/L/R) enabled. */
	apds9960_write_reg(APDS9960_REG_GPENTH, 40);
	apds9960_write_reg(APDS9960_REG_GEXTH, 30);
	apds9960_write_reg(APDS9960_REG_GCONF1, 0x40);
	apds9960_write_reg(APDS9960_REG_GCONF2, 0x41);
	apds9960_write_reg(APDS9960_REG_GPULSE, 0xC9);
	apds9960_write_reg(APDS9960_REG_GCONF3, 0x00);
	apds9960_write_reg(APDS9960_REG_GCONF4, 0x00); /* let the engine auto-enter/exit via thresholds */

	apds9960_write_reg(APDS9960_REG_ENABLE, APDS9960_ENABLE_PON);
	HAL_Delay(10); /* let the internal oscillator start up before enabling the engines */
	apds9960_write_reg(APDS9960_REG_ENABLE,
	                    APDS9960_ENABLE_PON | APDS9960_ENABLE_AEN | APDS9960_ENABLE_PEN | APDS9960_ENABLE_GEN);
}

/* Drains the gesture FIFO while a gesture is in progress, keeping
 * only the first and last (U,D,L,R) samples. Once the object leaves
 * gesture range (GVALID drops), compares first vs last to decide a
 * direction -- see the file header comment for the caveats of this
 * simplified approach. */
static gesture_t apds9960_poll_gesture(int32_t *out_diff_ud, int32_t *out_diff_lr)
{
	static uint8_t in_progress = 0;
	static int32_t first_u, first_d, first_l, first_r;
	static int32_t last_u, last_d, last_l, last_r;

	uint8_t status = apds9960_read_reg(APDS9960_REG_GSTATUS);

	if ((status & APDS9960_GSTATUS_GVALID) != 0U)
	{
		uint8_t level = apds9960_read_reg(APDS9960_REG_GFLVL);

		for (uint8_t i = 0; i < level; i++)
		{
			uint8_t udlr[4];

			if (HAL_I2C_Mem_Read(&hi2c1, APDS9960_ADDR, APDS9960_REG_GFIFO_U,
			                      I2C_MEMADD_SIZE_8BIT, udlr, 4, 100) != HAL_OK)
			{
				break;
			}

			if (!in_progress)
			{
				first_u = udlr[0];
				first_d = udlr[1];
				first_l = udlr[2];
				first_r = udlr[3];
				in_progress = 1;
			}
			last_u = udlr[0];
			last_d = udlr[1];
			last_l = udlr[2];
			last_r = udlr[3];
		}

		return GESTURE_NONE; /* gesture still in progress */
	}

	if (!in_progress)
	{
		return GESTURE_NONE; /* nothing happened since the last check */
	}

	in_progress = 0;

	int32_t diff_ud = (last_u - last_d) - (first_u - first_d);
	int32_t diff_lr = (last_l - last_r) - (first_l - first_r);
	int32_t abs_ud  = (diff_ud < 0) ? -diff_ud : diff_ud;
	int32_t abs_lr  = (diff_lr < 0) ? -diff_lr : diff_lr;

	if (out_diff_ud != NULL)
	{
		*out_diff_ud = diff_ud;
	}
	if (out_diff_lr != NULL)
	{
		*out_diff_lr = diff_lr;
	}

	/* Require the winning axis to clearly dominate the other
	 * (GESTURE_AXIS_MARGIN_PCT), not just edge it out by a hair --
	 * a real up/down swipe barely nudges the left/right diodes and
	 * vice versa, so a close call between the two almost always means
	 * noise, not a real diagonal gesture. */
	if ((abs_ud > GESTURE_DIFF_THRESHOLD) &&
	    (abs_ud > (abs_lr + (abs_lr * GESTURE_AXIS_MARGIN_PCT) / 100)))
	{
		/* UP/DOWN registers were swapped in practice on this board's
		 * orientation vs. the datasheet's default assumption, hence
		 * the flipped comparison here (verified against real swipes). */
		return (diff_ud > 0) ? GESTURE_DOWN : GESTURE_UP;
	}
	if ((abs_lr > GESTURE_DIFF_THRESHOLD) &&
	    (abs_lr > (abs_ud + (abs_ud * GESTURE_AXIS_MARGIN_PCT) / 100)))
	{
		/* Same story as UP/DOWN above: LEFT/RIGHT came out swapped
		 * against real swipes, flipped here to match. */
		return (diff_lr > 0) ? GESTURE_RIGHT : GESTURE_LEFT;
	}

	return GESTURE_NONE; /* too ambiguous between axes, or below threshold -- don't guess */
}

static const char *gesture_name(gesture_t g)
{
	switch (g)
	{
		case GESTURE_UP:    return "UP";
		case GESTURE_DOWN:  return "DOWN";
		case GESTURE_LEFT:  return "LEFT";
		case GESTURE_RIGHT: return "RIGHT";
		default:            return "?";
	}
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
