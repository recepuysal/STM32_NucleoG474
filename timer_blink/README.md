# timer_blink

NUCLEO-G474RE kartında kullanıcı LED'ini (**LD2**) `HAL_Delay()` **kullanmadan**,
tamamen **TIM3 donanım zamanlayıcısının kesmesiyle (interrupt)** her 500 ms'de
bir yakıp söndüren örnek. Önceki iki örnekten (`buton_on_off`, `buton_toggle`)
farkı: bu proje CMSIS/HAL kütüphanesini gerçekten içeriyor
(`Drivers/CMSIS`, `Drivers/STM32G4xx_HAL_Driver`) ve `HAL_TIM_Base_Start_IT()`
/ `HAL_TIM_PeriodElapsedCallback()` gibi gerçek HAL API'lerini kullanıyor.

## Neden önemli

Arduino'daki `delay()` CPU'yu bloke ederek bekler. STM32'de asıl güç, işi
donanım zamanlayıcısına bırakıp CPU'yu serbest bırakmaktır: `main()` içinde
`for(;;) { }` dışında hiçbir şey yok — LED'i yakıp söndüren kod tamamen kesme
içinde çalışıyor.

## Çalışma mantığı

```
main() → HAL_Init() → GPIO+TIM3 kurulum → HAL_TIM_Base_Start_IT() → for(;;){}  (boş)
                                                                          │
                                              500 ms'de bir donanım kesme üretir
                                                                          ▼
                                              TIM3_IRQHandler() → HAL_TIM_IRQHandler()
                                                                          ▼
                                                      HAL_TIM_PeriodElapsedCallback()
                                                                          ▼
                                                       HAL_GPIO_TogglePin(LED)
```

### Timer / Prescaler / Period ilişkisi

TIM3, APB1 hattına bağlı ve bu projede varsayılan saatle (HSI, hiç
bölünmemiş) **16 MHz** ile besleniyor. `Prescaler`, bu 16 MHz'lik darbeyi
daha yavaş bir "tick" frekansına böler:

```
tick_frekansı = TIM3_CLK / (Prescaler + 1) = 16.000.000 / 16000 = 1000 Hz  →  1 tick = 1 ms
```

`Period` (Auto-Reload Register / ARR) ise sayacın kaç tick sayıp sıfırlanacağını,
yani kaç tick'te bir "update event" (ve dolayısıyla kesme) üretileceğini belirler:

```
kesme_periyodu = (Period + 1) × tick_süresi = 500 × 1 ms = 500 ms
```

[main.c](Src/main.c) içinde bu değerler `htim3.Init.Prescaler = 15999` ve
`htim3.Init.Period = 499` olarak ayarlanmıştır (0'dan başladığı için -1 ile).

### Kod parçaları

- **`MX_GPIO_Init()`** — PA5'i (LD2) `HAL_GPIO_Init()` ile output yapar.
- **`MX_TIM3_Init()`** — `htim3` struct'ını Prescaler/Period değerleriyle
  doldurup `HAL_TIM_Base_Init()` ile TIM3'ü programlar, ardından
  `HAL_NVIC_SetPriority()` + `HAL_NVIC_EnableIRQ(TIM3_IRQn)` ile NVIC'te
  (kesme denetleyicisi) TIM3 kesmesini açar. Bu adım olmadan timer sayar
  ama CPU'yu hiç bölmez.
- **`HAL_TIM_Base_Start_IT(&htim3)`** — timer'ı fiilen başlatan (`CEN` biti)
  **ve** update-interrupt'ını (`UIE` biti) etkinleştiren fonksiyon. Sondaki
  "IT" (Interrupt) eki, kesmesiz karşılığı olan `HAL_TIM_Base_Start()`'tan
  ayırt eder.
- **`TIM3_IRQHandler(void)`** — startup dosyasındaki vektör tablosunda TIM3
  için ayrılmış gerçek kesme fonksiyonu. İçeriği sadece
  `HAL_TIM_IRQHandler(&htim3)`'e devretmektir; bayrakları kontrol edip doğru
  callback'i çağırma işini HAL üstlenir.
- **`HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)`** — HAL içinde
  **weak (zayıf)** tanımlı bir fonksiyon; burada override edilerek gerçek iş
  (LED toggle) yapılır. `htim->Instance == TIM3` kontrolü, projede birden
  fazla timer olsaydı hangisinin tetiklediğini ayırt etmek için gereklidir.
- **`SysTick_Handler()`** — `HAL_Delay()` kullanılmasa da HAL'in iç zaman
  aşımı (timeout) mekanizmaları `HAL_GetTick()`'e dayanır; bu da SysTick
  kesmesinin her 1 ms'de `HAL_IncTick()` çağırmasını gerektirir.

## Pin bağlantıları

| Sinyal | Pin | Açıklama |
|---|---|---|
| LD2 (kullanıcı LED'i) | PA5 | Output, HAL_GPIO_TogglePin ile her 500 ms'de bir tersine döner |

## Derleme ve yükleme

STM32Cube for VS Code eklentisiyle gelen `cube-cmake` ve `cube` araçları kullanılır.

```bash
cube-cmake --preset Debug
cube-cmake --build --preset Debug
cube programmer -c port=SWD -w build/Debug/STM32G474RE.elf -v -rst
```

Kart, ST-Link programlayıcısı üzerinden USB ile bilgisayara bağlı olmalıdır.

## Klasör yapısı hakkında

`Drivers/` klasörü, STMicroelectronics'in resmi CMSIS ve HAL kaynak
kodlarından (STM32CubeG4 firmware paketi) yalnızca bu örnek için gereken
minimal alt kümeyi içerir: `RCC`, `GPIO`, `DMA`, `EXTI`, `FLASH`, `PWR`,
`CORTEX` ve `TIM` modülleri. `Inc/stm32g4xx_hal_conf.h`, hangi HAL
modüllerinin derlemeye dahil edildiğini kontrol eden yapılandırma dosyasıdır.
