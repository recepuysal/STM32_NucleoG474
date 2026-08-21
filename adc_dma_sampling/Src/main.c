/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : ADC continuously sampled via DMA (no CPU polling),
 *                    LED brightness tracks the reading live
 ******************************************************************************
 */

#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* NUCLEO-G474RE:
 * - Potentiometer wiper on PA0 (ADC1_IN1), same wiring as
 *   adc_pwm_dimmer: outer legs to 3V3 and GND.
 * - User LED LD2 -> PA5, driven as PWM via TIM2_CH1 (AF1), same setup
 *   as adc_pwm_dimmer. Brightness is updated every single pass through
 *   main()'s loop straight from the DMA-computed average -- turning
 *   the pot changes the LED live, with no visible delay. That
 *   immediacy is the point: it proves the average really is being
 *   refreshed continuously in the background by DMA, not just once
 *   and then left stale, since main() never issues a fresh ADC
 *   request itself.
 * - USART2 (PA2=TX, PA3=RX, ST-Link VCP, 115200 8N1) reports the
 *   same averaged reading for anyone watching a terminal instead of
 *   the LED.
 *
 * Every earlier ADC project (adc_pwm_dimmer, ntc_temperature) polls:
 * the main loop calls HAL_ADC_PollForConversion() and only moves on
 * once a fresh sample is ready -- the CPU is tied up waiting on the
 * ADC every single time. This project hands the whole job to DMA
 * instead: HAL_ADC_Start_DMA() tells DMA1 to copy every new 12-bit
 * conversion straight into a circular RAM buffer completely on its
 * own. main()'s loop never touches the ADC or DMA at all; it just
 * reads whatever the DMA completion callbacks last computed.
 *
 * STM32G4 routes DMA requests through a DMAMUX (unlike older STM32
 * families where each peripheral was hardwired to one or two fixed
 * DMA channels), so any DMA channel can serve any peripheral as long
 * as its DMAMUX "request" field names the right source
 * (DMA_REQUEST_ADC1 here). */

#define ADC_DMA_BUF_LEN 16U /* must be even: split into two halves for double-buffering */
#define PWM_PERIOD       999U /* TIM2 @ 1 MHz / 1000 = 1 kHz PWM, no visible flicker */

ADC_HandleTypeDef  hadc1;
DMA_HandleTypeDef  hdma_adc1;
TIM_HandleTypeDef  htim2;
UART_HandleTypeDef huart2;

static uint32_t adc_dma_buf[ADC_DMA_BUF_LEN];
static volatile uint32_t latest_average = 0;
static volatile uint32_t dma_events     = 0; /* counts callback firings -- shows DMA really is running non-stop in the background */

static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM2_PWM_Init(void);
static void MX_USART2_UART_Init(void);
static void uart_send(const char *s);
static uint32_t average_buffer(const uint32_t *buf, uint32_t start, uint32_t len);

int main(void)
{
	HAL_Init();
	MX_GPIO_Init();
	MX_DMA_Init();
	MX_ADC1_Init();
	MX_TIM2_PWM_Init();
	MX_USART2_UART_Init();

	uart_send("ADC+DMA continuous sampling starting...\r\n");

	HAL_ADC_Start_DMA(&hadc1, adc_dma_buf, ADC_DMA_BUF_LEN);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

	uint32_t last_print = 0;

	/* main() genuinely never reads the ADC or the DMA buffer itself --
	 * every number printed here (and every brightness level shown on
	 * the LED) was already computed by a DMA completion callback
	 * before this loop ever looks at it. The LED line below runs every
	 * single pass through the loop, not gated by any timer, so it
	 * tracks the pot with no perceptible lag. */
	for (;;)
	{
		uint32_t duty = (latest_average * (PWM_PERIOD + 1U)) / 4096U;
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);

		if ((HAL_GetTick() - last_print) >= 300U)
		{
			char line[64];
			unsigned percent = (unsigned)((duty * 100U) / (PWM_PERIOD + 1U));

			snprintf(line, sizeof(line), "ADC (DMA avg): %4lu  LED: %3u%%   DMA callbacks so far: %lu\r\n",
			         (unsigned long)latest_average, percent, (unsigned long)dma_events);
			uart_send(line);

			last_print = HAL_GetTick();
		}
	}
}

/* Fires when DMA has just finished writing the FIRST half of the
 * circular buffer and has moved on to writing the second half --
 * indices [0, LEN/2) are stable to read right now. */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
	latest_average = average_buffer(adc_dma_buf, 0, ADC_DMA_BUF_LEN / 2U);
	dma_events++;
}

/* Fires when DMA has just finished writing the SECOND half and
 * wrapped back around to the start of the buffer -- indices
 * [LEN/2, LEN) are stable to read right now. Reading half the buffer
 * while DMA writes the other half (instead of one flag for the whole
 * buffer) is the standard "ping-pong" pattern for circular DMA: it
 * avoids ever reading a sample DMA is actively overwriting. */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
	latest_average = average_buffer(adc_dma_buf, ADC_DMA_BUF_LEN / 2U, ADC_DMA_BUF_LEN / 2U);
	dma_events++;
}

static uint32_t average_buffer(const uint32_t *buf, uint32_t start, uint32_t len)
{
	uint32_t sum = 0;

	for (uint32_t i = 0; i < len; i++)
	{
		sum += buf[start + i];
	}

	return sum / len;
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

	/* PA2/PA3: USART2 TX/RX, alternate function AF7 */
	GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* PA5: LD2 as TIM2_CH1 PWM output */
	GPIO_InitStruct.Pin       = GPIO_PIN_5;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_NOPULL;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_DMA_Init(void)
{
	__HAL_RCC_DMAMUX1_CLK_ENABLE();
	__HAL_RCC_DMA1_CLK_ENABLE();

	hdma_adc1.Instance                 = DMA1_Channel1;
	hdma_adc1.Init.Request             = DMA_REQUEST_ADC1;
	hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
	hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
	hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
	hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
	hdma_adc1.Init.Mode                = DMA_CIRCULAR; /* wraps back to buf[0] forever, never stops on its own */
	hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW;
	HAL_DMA_Init(&hdma_adc1);

	__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

	HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* NOTE: this vendored startup_stm32g474xx.S names this vector
 * "DMA1_CH1_IRQHandler" (not the more common "DMA1_Channel1_IRQHandler"
 * seen in other CubeMX-generated projects). Using the wrong name here
 * silently compiles fine but never actually overrides the weak vector
 * -- the real vector table entry stays bound to Default_Handler
 * (an infinite loop), so the whole board hangs the instant the first
 * DMA interrupt fires. Always check the startup .S file's exact
 * symbol name before naming an IRQ handler. */
void DMA1_CH1_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_adc1);
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
	hadc1.Init.DMAContinuousRequests = ENABLE; /* keep re-arming DMA forever, not just for one buffer pass */
	hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
	hadc1.Init.OversamplingMode      = DISABLE;
	HAL_ADC_Init(&hadc1);

	HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

	sConfig.Channel      = ADC_CHANNEL_1; /* PA0 */
	sConfig.Rank         = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
	sConfig.SingleDiff   = ADC_SINGLE_ENDED;
	sConfig.OffsetNumber = ADC_OFFSET_NONE;
	sConfig.Offset       = 0;
	HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

static void MX_TIM2_PWM_Init(void)
{
	TIM_OC_InitTypeDef sConfigOC = { 0 };

	__HAL_RCC_TIM2_CLK_ENABLE();

	htim2.Instance               = TIM2;
	htim2.Init.Prescaler         = 15; /* 16 MHz / 16 = 1 MHz counter clock */
	htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
	htim2.Init.Period            = PWM_PERIOD; /* 1 MHz / 1000 = 1 kHz PWM */
	htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	HAL_TIM_PWM_Init(&htim2);

	sConfigOC.OCMode     = TIM_OCMODE_PWM1;
	sConfigOC.Pulse      = 0; /* start at 0% duty (LED off) */
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);
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

void SysTick_Handler(void)
{
	HAL_IncTick();
}
