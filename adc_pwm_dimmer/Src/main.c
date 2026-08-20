/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Potentiometer -> ADC -> PWM -> LED brightness
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - Potentiometer wiper -> PA0 (ADC1_IN1, Arduino A0). Outer legs go to
 *   3V3 and GND.
 * - User LED LD2 -> PA5, driven as PWM via TIM2_CH1 (AF1).
 * - USART2 (PA2=TX, PA3=RX, ST-Link VCP, 115200 8N1) streams the
 *   current ADC/duty reading so it can be watched from a serial
 *   terminal on the PC, no need to look at the LED at all.
 *
 * TIM2 is on APB1, clocked at 16 MHz by default (HSI, no prescalers
 * touched). Prescaler 15 -> 16 MHz / 16 = 1 MHz counter clock.
 * Period 999 -> 1000 counts per cycle = 1 kHz PWM (no visible flicker).
 *
 * The ADC is 12-bit (0..4095) and the PWM duty register (CCR1) only
 * needs to range 0..999, so each ADC sample is rescaled with
 * duty = adc_value * (ARR + 1) / 4096 -- this maps 0V..3.3V linearly
 * onto 0%..100% brightness. */

#define PWM_PERIOD 999U
#define PRINT_INTERVAL_MS 200U

static TIM_HandleTypeDef htim2;
static ADC_HandleTypeDef hadc1;
static UART_HandleTypeDef huart2;

static void MX_GPIO_Init(void);
static void MX_TIM2_PWM_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);

int main(void)
{
	uint32_t last_print = 0;

	HAL_Init();
	MX_GPIO_Init();
	MX_TIM2_PWM_Init();
	MX_ADC1_Init();
	MX_USART2_UART_Init();

	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
	HAL_ADC_Start(&hadc1);

	for (;;)
	{
		if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
		{
			uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
			uint32_t duty = (adc_value * (PWM_PERIOD + 1U)) / 4096U;

			__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);

			if ((HAL_GetTick() - last_print) >= PRINT_INTERVAL_MS)
			{
				char line[48];
				unsigned percent = (unsigned)((duty * 100U) / (PWM_PERIOD + 1U));

				snprintf(line, sizeof(line), "ADC: %4lu  Duty: %3u%%\r\n",
				         (unsigned long)adc_value, percent);
				HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)strlen(line), HAL_MAX_DELAY);

				last_print = HAL_GetTick();
			}
		}
	}
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();

	/* PA0: analog input for the potentiometer wiper */
	GPIO_InitStruct.Pin  = GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* PA5: LD2 as TIM2_CH1 PWM output */
	GPIO_InitStruct.Pin       = GPIO_PIN_5;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* PA2/PA3: USART2 TX/RX, alternate function AF7 */
	GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_TIM2_PWM_Init(void)
{
	TIM_OC_InitTypeDef sConfigOC = { 0 };

	__HAL_RCC_TIM2_CLK_ENABLE();

	htim2.Instance               = TIM2;
	htim2.Init.Prescaler         = 15;
	htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
	htim2.Init.Period            = PWM_PERIOD;
	htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	HAL_TIM_PWM_Init(&htim2);

	sConfigOC.OCMode     = TIM_OCMODE_PWM1;
	sConfigOC.Pulse       = 0; /* start at 0% duty (LED off) */
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);
}

static void MX_ADC1_Init(void)
{
	ADC_ChannelConfTypeDef sConfig = { 0 };

	__HAL_RCC_ADC12_CLK_ENABLE();

	hadc1.Instance                   = ADC1;
	hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
	hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
	hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
	hadc1.Init.GainCompensation      = 0;
	hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
	hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
	hadc1.Init.LowPowerAutoWait      = DISABLE;
	hadc1.Init.ContinuousConvMode    = ENABLE;
	hadc1.Init.NbrOfConversion       = 1;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
	hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
	hadc1.Init.SamplingMode          = ADC_SAMPLING_MODE_NORMAL;
	hadc1.Init.DMAContinuousRequests = DISABLE;
	hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
	hadc1.Init.OversamplingMode      = DISABLE;
	HAL_ADC_Init(&hadc1);

	/* G4's ADC needs a self-calibration pass for accurate readings;
	 * this measures and stores an internal offset correction. */
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

	sConfig.Channel      = ADC_CHANNEL_1; /* PA0 */
	sConfig.Rank         = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
	sConfig.SingleDiff   = ADC_SINGLE_ENDED;
	sConfig.OffsetNumber = ADC_OFFSET_NONE;
	sConfig.Offset       = 0;
	HAL_ADC_ConfigChannel(&hadc1, &sConfig);
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

void SysTick_Handler(void)
{
	HAL_IncTick();
}
