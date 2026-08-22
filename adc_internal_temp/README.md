# adc_internal_temp

STM32G474'ün **dahili sıcaklık sensörünü** ADC ile okuyup her saniye
**USART2 üzerinden (ST-Link VCP) seri terminale** akıtan örnek. Ek
olarak kullanıcı butonuna (**B1**) her basıldığında kullanıcı LED'i
(**LD2**) [exti_button_led](../exti_button_led) ile aynı EXTI kesmesi
mantığıyla açılıp kapanıyor ve durumu da seri porta yazılıyor.

Ekstra donanım bağlantısı gerekmez — sensör çipin içinde, B1/LD2 da
kartın üzerinde.

Gerçek donanımda doğrulandı — oda sıcaklığında (kart bir süre çalışıp
kendi kendine ısındıktan sonra) örnek çıktı:

```
Sicaklik: 33 C
Sicaklik: 34 C
Sicaklik: 34 C
LED YANDI
Sicaklik: 34 C
LED SONDU
Sicaklik: 34 C
```

> Dahili sensör kartın/çipin sıcaklığını ölçer, **oda sıcaklığını
> değil** — bu yüzden değer genelde ortamdan birkaç derece yüksek
> çıkar (çipin kendi kendine ısınması). Parmağınızı kartın üzerine
> koyup birkaç saniye beklerseniz değerin yükseldiğini görebilirsiniz.

## Terminalden izleme

Kartın ST-Link COM portuna **115200 8N1** ile bağlanınca 1 saniyede
bir `Sicaklik: xx C` satırı akar. Butona basıldıkça araya `LED YANDI`
/ `LED SONDU` satırları karışır. Bu proje de sadece **TX** kullanır,
komut göndermeye gerek yoktur.

## ⚠️ Önemli not: ADC saat hızı — "217 C" hatası

İlk denemede ADC saat bölücüsü `ADC_CLOCK_ASYNC_DIV1` (bölmesiz)
olarak ayarlanmıştı. Bu, ADC'yi doğrudan sistem saatiyle (bu projede
170 MHz, `SystemClock_Config()`'teki PLL ayarından) çalıştırıyordu —
ama STM32G474'ün ADC'si maksimum ~60 MHz'e kadar destekliyor. Sonuç:
ADC izin verilenin ~3 katı hızda çalıştırılınca örnekleme devresi
düzgün çalışamadı ve **tutarlı ama tamamen yanlış** bir değer üretti
(`Sicaklik: 217 C` — gerçek sıcaklık kalibrasyon formülüyle hesaba
katılınca fiziksel olarak imkânsız bir sayı, formülün kendisi
matematiksel olarak doğru olmasına rağmen).

**Çözüm:** `hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;` — ADC
saati artık 170/4 = 42.5 MHz, spesifikasyon içinde. Bu değişiklikten
sonra okuma hemen makul bir aralığa (33-34°C) düştü.

Bu, "kod derleniyor ve çalışıyor gibi görünüyor ama sonuç saçma"
türünden klasik bir donanım yapılandırma hatasıydı — derleyici veya
linker hiçbir uyarı vermez, çünkü saat bölücüsü değeri sözdizimsel
olarak tamamen geçerlidir; sadece çipin elektriksel limitlerini aşar.

### Kod parçaları — [main.c](Core/Src/main.c)

- **`VDDA_APPLI`**: Kartın besleme gerilimi (mV), sıcaklık hesabında
  ADC ham değerini gerçek voltaja çevirmek için kullanılır. Nucleo
  kartlarda ST-Link'in ürettiği besleme ~3.3V olduğundan `3300`
  kullanıldı.
- **`MX_ADC1_Init()`**: ADC1'i `ADC_CHANNEL_TEMPSENSOR_ADC1` kanalına
  bağlar — bu, çipin *içindeki* sıcaklık sensörüne bağlı özel bir ADC
  kanalıdır (dışarıdan bir pime bağlı değildir). `HAL_ADC_ConfigChannel()`
  bu kanal seçildiğinde dahili sensör yolunu (ve gerekli stabilizasyon
  gecikmesini) otomatik olarak devreye sokar — ayrıca bir register
  ayarına gerek yoktur.
- **`SystemClock_Config()`**'e eklenen `RCC_PeriphCLKInitTypeDef`:
  ADC12 çevre birimi saatinin kaynağını (`RCC_ADC12CLKSOURCE_SYSCLK`)
  ve az önceki notta anlatılan bölücüyü ayarlar.
- **`HAL_ADCEx_Calibration_Start()`**: Her açılışta ADC'nin kendi
  kendini kalibre etmesini sağlar (sıcaklık/gerilim kaynaklı sapmaları
  telafi eder) — bu, sıcaklık sensörünün fabrika kalibrasyon
  değerlerinden (`TS_CAL1`/`TS_CAL2`, üretimde çipin içine yazılmış,
  30°C ve 130°C'de ölçülmüş referans değerler) **farklı** bir şeydir.
- **`__LL_ADC_CALC_TEMPERATURE()`**: `TS_CAL1`/`TS_CAL2` fabrika
  değerleri ile ADC'den okunan ham veri arasında doğrusal
  interpolasyon yaparak °C cinsinden sıcaklığı hesaplayan hazır makro
  (`stm32g4xx_ll_adc.h`).
- **`BSP_PB_Callback()`**: `exti_button_led`'deki gibi EXTI kesmesiyle
  tetiklenir (ama burada BSP'nin kendi buton soyutlaması üzerinden);
  `BSP_LED_GetState()` ile LED'in mevcut durumunu okuyup tersine
  çevirir ve durumu `printf()` ile seri porta yazar.
- **`printf()` yönlendirmesi**: Ayrı bir `__io_putchar()` **yazılmadı** —
  BSP kütüphanesi (`stm32g4xx_nucleo.c`) `USE_COM_LOG` açıkken bunu
  zaten sağlıyor. İlk denemede bu fark edilmeden ikinci bir
  `__io_putchar()` tanımlanmıştı, bu da linker'da
  `multiple definition of '__io_putchar'` hatasına yol açtı; kaldırılınca
  düzeldi.

## Derleme ve yükleme

Bu klasör, STM32Cube for VS Code eklentisinin **yeni** proje
şablonuyla oluşturuldu (bkz. aşağıdaki "Klasör yapısı hakkında"), ama
aynı `cube-cmake` / `cube` araçlarıyla derlenip yükleniyor:

```bash
cd adc_internal_temp
cube-cmake --preset Debug
cube-cmake --build --preset Debug
cube programmer -c port=SWD -w build/Debug/adc_internal_temp.elf -v -rst
```

Kart, ST-Link programlayıcısı üzerinden USB ile bilgisayara bağlı olmalıdır.

## Klasör yapısı hakkında

Bu klasör, depodaki **diğer tüm örneklerden farklı bir şablonla**
oluşturuldu:

| | Diğer örnekler | `adc_internal_temp` |
|---|---|---|
| Kaynak klasörleri | `Inc/`, `Src/` | `Core/Inc/`, `Core/Src/` |
| LED/buton/UART erişimi | Ham HAL (`HAL_GPIO_Init`, kendi `__io_putchar`'ı) | **NUCLEO BSP** kütüphanesi (`BSP_LED_Init`, `BSP_PB_Init`, `BSP_COM_Init`) |
| CMake yapısı | `cmake/components.cmake` + `cmake/files.cmake` | Tek `cmake/stm32cubemx/CMakeLists.txt` |

Bunun sebebi, bu projenin daha yeni bir STM32Cube for VS Code
sürümüyle/şablonuyla oluşturulmuş olması. İşlevsel olarak bir fark
yaratmaz — build/flash adımları yukarıdaki gibi aynı şekilde çalışır —
ama kaynak koduna bakarken bu farkı bilmek karışıklığı önler.

`Drivers/` klasörü yine yalnızca bu örnek için gereken minimal HAL alt
kümesini içerir: `RCC`, `GPIO`, `DMA`, `EXTI`, `FLASH`, `PWR`,
`CORTEX`, `UART` ve `ADC` (+ `LL_ADC` header'ı, `HAL_ADC_ConfigChannel()`
içindeki dahili kanal/kalibrasyon fonksiyonları için). NUCLEO BSP
kaynağı `Drivers/BSP/STM32G4xx_Nucleo/stm32g4xx_nucleo.c`'de yer alır.
`Core/Inc/stm32g4xx_hal_conf.h`, hangi HAL modüllerinin derlemeye
dahil edildiğini kontrol eden yapılandırma dosyasıdır.
