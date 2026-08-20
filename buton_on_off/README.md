# buton_on_off

NUCLEO-G474RE kartında kullanıcı butonuna (**B1**) basılı tutulduğu sürece
kullanıcı LED'ini (**LD2**) yakan, bırakılınca söndüren basit bir örnek.

Gerçek STM32 HAL kütüphanesi kullanılır (`Drivers/CMSIS`,
`Drivers/STM32G4xx_HAL_Driver`); GPIO'lara `HAL_GPIO_Init`,
`HAL_GPIO_ReadPin` ve `HAL_GPIO_WritePin` üzerinden erişilir.

## Pin bağlantıları

| Sinyal | Pin | Açıklama |
|---|---|---|
| LD2 (kullanıcı LED'i) | PA5 | Output, HIGH = LED yanık |
| B1 (kullanıcı butonu) | PC13 | Input, dahili pull-down; basılınca HIGH okunur |

> Not: Bu karttaki B1 butonu basıldığında PC13'ü VDD'ye (HIGH) çeker.
> Bu yüzden boşta kararlı LOW okumak için `GPIO_InitStruct.Pull = GPIO_PULLDOWN`
> ile dahili pull-down kullanılmıştır.

## Çalışma mantığı

`main.c` sonsuz döngüde `HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13)` ile PC13'ü okur:
- **`GPIO_PIN_SET`** (buton basılı) → `HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET)` → LED yanar
- **`GPIO_PIN_RESET`** (buton bırakılmış) → LED söner

`HAL_Init()` çağrısı SysTick'i 1 ms tetiklemeye ayarladığından, HAL'in iç
zamanlama mekanizmasının çalışması için `SysTick_Handler()` içinde
`HAL_IncTick()` çağrılması gerekir; bu proje `HAL_Delay` kullanmasa da bu
tanım zorunludur, aksi halde SysTick kesmesi tanımsız (varsayılan sonsuz
döngü) handler'a düşer ve kart açılışta kilitlenir.

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
minimal alt kümeyi içerir. `Inc/stm32g4xx_hal_conf.h`, hangi HAL modüllerinin
derlemeye dahil edildiğini kontrol eden yapılandırma dosyasıdır.
