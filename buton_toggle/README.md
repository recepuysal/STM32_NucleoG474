# buton_toggle

NUCLEO-G474RE kartında kullanıcı butonuna (**B1**) her basıldığında kullanıcı
LED'inin (**LD2**) durumunu tersine çeviren örnek: basınca yak, tekrar basınca
söndür. [buton_on_off](../buton_on_off) örneğinden farkı, LED'in butona basılı
kalma süresine değil, sadece basma **anına** tepki vermesidir.

Gerçek STM32 HAL kütüphanesi kullanılır (`Drivers/CMSIS`,
`Drivers/STM32G4xx_HAL_Driver`); GPIO'lara `HAL_GPIO_Init`,
`HAL_GPIO_ReadPin` ve `HAL_GPIO_TogglePin` üzerinden, debounce beklemesine
`HAL_Delay` üzerinden erişilir.

## Pin bağlantıları

| Sinyal | Pin | Açıklama |
|---|---|---|
| LD2 (kullanıcı LED'i) | PA5 | Output, HIGH = LED yanık |
| B1 (kullanıcı butonu) | PC13 | Input, dahili pull-down; basılınca HIGH okunur |

> Not: Bu karttaki B1 butonu basıldığında PC13'ü VDD'ye (HIGH) çeker, bu yüzden
> boşta kararlı LOW okumak için dahili pull-down (`GPIO_PULLDOWN`) kullanılmıştır.

## Çalışma mantığı

Her buton basımı, döngünün milyonlarca kez çalıştığı süre boyunca "basılı"
olarak defalarca okunur. Sadece basılı/basılı-değil geçişinde (**rising
edge**) bir kez toggle yapmak için önceki tur ile şimdiki turun durumu
karşılaştırılır:

1. `was_pressed` bir önceki döngüdeki buton durumunu tutar.
2. `pressed_now && !was_pressed` şartı, "az önce basılı değildi, şimdi basılı"
   geçişini, yani butona **yeni basıldığı** anı yakalar.
3. Bu an yakalanınca `HAL_Delay(50)` ile kısa bir **debounce** gecikmesi
   uygulanır; mekanik buton kontakları basıldığı anda birkaç kez çok hızlı
   açılıp kapanabildiğinden (sıçrama), bu bekleme sahte çoklu geçişleri eler.
4. Bekleme sonunda buton hâlâ basılıysa gerçek bir basmadır ve
   `HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5)` ile LED'in durumu tersine çevrilir.
5. `was_pressed` güncellenir ve döngü buton bırakılana kadar (ve sonraki
   basışa kadar) tekrar tetiklenmez.

`HAL_Delay()`'in çalışabilmesi (ve genel olarak HAL'in iç zamanlama
mekanizmasının doğru işlemesi) için `SysTick_Handler()` içinde
`HAL_IncTick()` çağrılması zorunludur; bu tanım olmadan SysTick kesmesi
varsayılan (sonsuz döngü) handler'a düşer ve kart açılışta kilitlenir.

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
