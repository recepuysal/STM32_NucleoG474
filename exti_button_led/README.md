# exti_button_led

Kullanıcı butonuna (**B1**) her basıldığında kullanıcı LED'inin
(**LD2**) durumunu tersine çeviren örnek — [buton_toggle](../buton_toggle)
ile **aynı davranış**, ama tamamen farklı bir mekanizmayla: polling
yerine gerçek bir **donanım kesmesi (EXTI)**.

Ekstra donanım bağlantısı gerekmez — B1 ve LD2 kartın üzerinde.

## buton_toggle'dan farkı

| | `buton_toggle` | `exti_button_led` |
|---|---|---|
| Buton nasıl okunur | `main()` döngüsünde sürekli `HAL_GPIO_ReadPin()` (polling) | `GPIO_MODE_IT_RISING` ile donanım kesmesi |
| `main()` içeriği | Buton durumunu karşılaştıran bir döngü | Tamamen boş `for(;;){}` |
| Debounce | `HAL_Delay(50)` (bloklayan bekleme) | Kesme içinde zaman damgası karşılaştırması (bloklamayan) |
| LED'i kim değiştirir | `main()` döngüsü | `HAL_GPIO_EXTI_Callback()` (kesme bağlamında) |

Polling'in dezavantajı: CPU sürekli "basıldı mı, basıldı mı?" diye
sormak zorunda, bu da hem gereksiz güç tüketir hem de döngüde başka
bir iş varsa (örn. `oled_i2c_display`'deki gibi ekran güncelleme)
tepki süresini gecikmeye açık hale getirir. Kesme ile CPU butonu hiç
"düşünmez" — sadece gerçekten bir kenar (edge) geldiğinde donanım
otomatik olarak ilgili fonksiyonu çağırır.

## Çalışma mantığı

```
Buton basılır → PC13 LOW'dan HIGH'a çıkar (rising edge)
                        │
              EXTI13 donanımı bunu algılar
                        │
              NVIC, EXTI15_10_IRQHandler()'ı tetikler
                        │
              HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13) çağrılır
              (hangi hattın tetiklendiğini kontrol edip pending bitini temizler)
                        │
              HAL_GPIO_EXTI_Callback(GPIO_PIN_13) çağrılır
                        │
              (debounce kontrolü) → LED toggle
```

### Kod parçaları — [main.c](Src/main.c)

- **`GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING`**: PC13'ü hem giriş
  yapar hem de üzerinde bir **kesme talebi** kurar; "LOW'dan HIGH'a
  geçişte beni haberdar et" der. Bu karttaki B1 butonu basılınca
  PC13'ü HIGH'a çektiği için (bkz. [buton_on_off](../buton_on_off)'taki
  polarite notu) burada rising edge doğru tetikleyicidir.
- **`EXTI15_10_IRQn`**: STM32'de GPIO kesmeleri pin numarasına göre
  gruplanır — pin 0-4'ün her biri kendi vektörüne sahipken, pin 5-9
  tek bir vektörü (`EXTI9_5`), pin 10-15 de tek bir vektörü
  (`EXTI15_10`) paylaşır. PC13, pin 13 olduğu için `EXTI15_10_IRQn`
  kullanılır — eğer aynı anda PB10 gibi başka bir pin de kesme
  kullansaydı, aynı vektörü paylaşırlardı ve `HAL_GPIO_EXTI_IRQHandler()`
  hangisinin tetiklendiğini pin numarasına bakarak ayırt ederdi.
- **`HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13)`**: HAL'in hazır fonksiyonu;
  ilgili EXTI "pending" bitini kontrol edip temizler (temizlenmezse
  kesme sürekli tekrar tetiklenir) ve `HAL_GPIO_EXTI_Callback()`'i çağırır.
- **`HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)`**: HAL'de weak
  tanımlı, burada override edilir. `GPIO_Pin` parametresi hangi pinin
  tetiklendiğini söyler (birden fazla pin kesme kullanıyorsa
  `if (GPIO_Pin == ...)` ile ayırt edilir).
- **Debounce**: Mekanik buton kontakları kapanır kapanmaz birkaç kez
  çok hızlı açılıp kapanabilir (sıçrama), bu da kısa sürede birden
  fazla kesme tetiklenmesine yol açar. Kesme bağlamında `HAL_Delay()`
  gibi bloklayan bir bekleme kullanmak kötü pratiktir (CPU'yu ve diğer
  kesmeleri gereksiz yere durdurur); bunun yerine bir önceki kabul
  edilen basıştan bu yana geçen süre `HAL_GetTick()` ile ölçülüp
  `DEBOUNCE_MS`'den (200ms) kısaysa kesme yok sayılır.

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
kodlarından yalnızca bu örnek için gereken minimal alt kümeyi içerir:
`RCC`, `GPIO`, `DMA`, `EXTI`, `FLASH`, `PWR` ve `CORTEX` modülleri.
EXTI kesmesi için ayrı bir kaynak dosyaya gerek yoktur —
`HAL_GPIO_EXTI_IRQHandler()` ve `HAL_GPIO_EXTI_Callback()` doğrudan
`stm32g4xx_hal_gpio.c` içinde tanımlıdır. `Inc/stm32g4xx_hal_conf.h`,
hangi HAL modüllerinin derlemeye dahil edildiğini kontrol eden
yapılandırma dosyasıdır.
