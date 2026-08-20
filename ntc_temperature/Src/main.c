/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : NTC thermistor -> ADC -> temperature, streamed over UART
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - NTC probe (bare 2-wire, ~10k ohm @25C, Beta ~3950) forms a voltage
 *   divider with a fixed 10k ohm resistor:
 *
 *       3V3 ---[ NTC ]---+---[ 10k ]--- GND
 *                         |
 *                        PA0 (ADC1_IN1)
 *
 *   As it gets hotter, the NTC's resistance drops, so more of the 3.3V
 *   appears across the fixed resistor -> the ADC reading goes UP with
 *   temperature.
 * - USART2 (PA2=TX, PA3=RX, ST-Link VCP, 115200 8N1) streams the
 *   computed temperature so it can be read from a PC terminal.
 *
 * Temperature is normally derived from the measured resistance with
 * the "Beta parameter" equation (1/T = 1/T0 + (1/B)*ln(R/R0)), but
 * that needs log() from libm. The exact bundled arm-none-eabi-gcc/ld
 * used by this project has a linker bug where calling log()/sqrtf()
 * (and friends) alongside this project's full HAL object set and
 * custom linker script fails with "undefined reference" + "Unknown
 * destination type (ARM/Thumb)" -- the archived libm object is
 * missing proper Thumb mapping symbols. Rather than fight the
 * toolchain, the Beta equation is pre-computed offline into a lookup
 * table and interpolated at runtime -- a standard embedded technique
 * that also completely avoids the broken function. */

#define NUM_POINTS 33

static const uint16_t ADC_POINTS[NUM_POINTS] = {
	1, 129, 257, 385, 513, 641, 769, 897, 1025, 1153, 1281, 1409, 1537,
	1665, 1793, 1921, 2049, 2177, 2305, 2433, 2561, 2689, 2817, 2945,
	3073, 3201, 3329, 3457, 3585, 3713, 3841, 3969, 4094
};

/* Celsius x10 (e.g. 235 = 23.5 C), computed offline from the Beta
 * equation for a 10k/3950 NTC with a 10k series resistor. */
static const int16_t TEMP_TENTHS[NUM_POINTS] = {
	-900, -363, -255, -185, -131, -86, -47, -11, 22, 53, 83, 112, 140,
	167, 195, 222, 250, 279, 308, 338, 370, 403, 439, 478, 520, 568,
	622, 686, 765, 868, 1019, 1300, 5279
};

#define PRINT_INTERVAL_MS 500U

static ADC_HandleTypeDef hadc1;
static UART_HandleTypeDef huart2;

static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
static int32_t adc_to_celsius_tenths(uint32_t adc_value);

int main(void)
{
	uint32_t last_print = 0;

	HAL_Init();
	MX_GPIO_Init();
	MX_ADC1_Init();
	MX_USART2_UART_Init();

	HAL_ADC_Start(&hadc1);

	for (;;)
	{
		if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
		{
			uint32_t adc_value = HAL_ADC_GetValue(&hadc1);

			if ((HAL_GetTick() - last_print) >= PRINT_INTERVAL_MS)
			{
				char line[64];
				int32_t t_tenths = adc_to_celsius_tenths(adc_value);
				int32_t t_abs    = (t_tenths < 0) ? -t_tenths : t_tenths;

				snprintf(line, sizeof(line), "ADC: %4lu  Temperature: %s%ld.%ld C\r\n",
				         (unsigned long)adc_value, (t_tenths < 0) ? "-" : "",
				         (long)(t_abs / 10), (long)(t_abs % 10));
				HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)strlen(line), HAL_MAX_DELAY);

				last_print = HAL_GetTick();
			}
		}
	}
}

/* Converts a raw 12-bit ADC reading from the NTC divider into degrees
 * Celsius (x10) by linearly interpolating the pre-computed lookup
 * table above -- see the comment at the top of the file for why a
 * table is used instead of calling log() directly. */
static int32_t adc_to_celsius_tenths(uint32_t adc_value)
{
	uint32_t i;
	uint32_t adc = adc_value;

	if (adc < ADC_POINTS[0])
	{
		adc = ADC_POINTS[0];
	}
	if (adc > ADC_POINTS[NUM_POINTS - 1])
	{
		adc = ADC_POINTS[NUM_POINTS - 1];
	}

	for (i = 0; i < (NUM_POINTS - 1U); i++)
	{
		if (adc <= ADC_POINTS[i + 1U])
		{
			int32_t adc_lo = ADC_POINTS[i];
			int32_t adc_hi = ADC_POINTS[i + 1U];
			int32_t t_lo   = TEMP_TENTHS[i];
			int32_t t_hi   = TEMP_TENTHS[i + 1U];

			return t_lo + ((int32_t)adc - adc_lo) * (t_hi - t_lo) / (adc_hi - adc_lo);
		}
	}

	return TEMP_TENTHS[NUM_POINTS - 1];
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();

	/* PA0: analog input for the NTC divider midpoint */
	GPIO_InitStruct.Pin  = GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* PA2/PA3: USART2 TX/RX, alternate function AF7 */
	GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
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
