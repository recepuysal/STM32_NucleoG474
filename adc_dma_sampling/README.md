# adc_dma_sampling

![Potansiyometre](potansiyometre-10k-1.jpg)

[adc_pwm_dimmer](../adc_pwm_dimmer) ve [ntc_temperature](../ntc_temperature)
ile aynı potansiyometre devresi, ama bu sefer ADC **DMA** ile okunuyor:
CPU, ADC'yi hiç yoklamıyor (poll etmiyor); DMA1 her yeni 12-bit
dönüşümü kendi başına dairesel (circular) bir RAM tamponuna yazıyor,
`main()` döngüsü sadece DMA'nın en son hesapladığı ortalamayı okuyup
seri porta basıyor.

Gerçek donanımda doğrulandı — potansiyometre çevrildikçe değer akıcı
şekilde 0-4095 arasında değişiyor, DMA geri çağırma (callback) sayacı
kesintisiz artıyor:

```
ADC (DMA avg): 2537   DMA callbacks so far: 27798
ADC (DMA avg): 2215   DMA callbacks so far: 42998
ADC (DMA avg):    1   DMA callbacks so far: 53131
ADC (DMA avg): 4095   DMA callbacks so far: 101265
ADC (DMA avg):  651   DMA callbacks so far: 136731
```

## Donanım bağlantısı

| Sinyal | Pin | Açıklama |
|---|---|---|
| Potansiyometre orta uç (wiper) | **PA0** | ADC1_IN1 |
| Potansiyometre bir dış ucu | 3V3 | |
| Potansiyometre diğer dış ucu | GND | |
| USART2 TX/RX | PA2 / PA3 | ST-Link VCP, 115200 8N1 |

## Neden DMA? Polling'e göre fark ne

Bu depodaki önceki iki ADC projesi (`adc_pwm_dimmer`, `ntc_temperature`)
**polling** kullanıyordu: `main()` döngüsü `HAL_ADC_PollForConversion()`
çağırıp yeni bir örnek hazır olana kadar bekliyordu — yani CPU, her
okuma için ADC'nin bitirmesini bekleyerek zaman kaybediyordu.

Bu projede CPU, ADC ile hiç konuşmuyor:

1. `HAL_ADC_Start_DMA()` bir kere çağrılır, DMA1'e "her yeni dönüşümü
   şu adrese yaz" der.
2. ADC `ContinuousConvMode` sayesinde kendi kendine sürekli dönüşüm
   yapar (~66 kHz civarı, 4 MHz ADC saatinde 47.5 örnekleme + 12.5
   dönüşüm çevrimi ile).
3. DMA her yeni 12-bit değeri otomatik olarak `adc_dma_buf[]`
   dizisine yazar, dizi dolunca **circular** modu sayesinde baştan
   yazmaya devam eder — hiç durmaz.
4. `main()` döngüsü hiçbir zaman ADC'ye veya DMA'ya dokunmaz; sadece
   en son DMA callback'inin hesapladığı `latest_average`'i okur.

## "Ping-pong" (yarım/tam tampon) deseni

16 elemanlık `adc_dma_buf[]` ikiye bölünmüş durumda. DMA dairesel
modda tamponu doldururken iki farklı kesme üretir:

- **`HAL_ADC_ConvHalfCpltCallback`**: tamponun **ilk yarısı**
  (`[0, 8)`) yeni yazılmış, DMA şimdi ikinci yarıyı yazmaya geçti —
  yani ilk yarı artık güvenle okunabilir.
- **`HAL_ADC_ConvCpltCallback`**: tamponun **ikinci yarısı**
  (`[8, 16)`) yeni yazılmış, DMA baştan (`[0, 8)`) yazmaya döndü —
  ikinci yarı artık güvenle okunabilir.

Bu düzen sayesinde DMA bir yarıyı yazarken CPU her zaman *öbür* yarıyı
okur — DMA'nın o an aktif olarak üzerine yazdığı veriyi okuma riski
hiç oluşmaz. Tek bir "tampon doldu" bayrağı yerine bu iki ayrı kesmeyi
kullanmak, dairesel DMA ile sürekli veri akışının standart yöntemidir.

## DMAMUX: STM32G4'e özgü esneklik

Eski STM32 ailelerinde (F1, F4 gibi) her peripheral, DMA kontrolcüsünün
sabit bir kanalına bağlıydı (örn. ADC1 → DMA1 Kanal 1, başka seçenek
yok). STM32G4'te ise araya bir **DMAMUX** girer: hangi DMA kanalının
hangi peripheral'e hizmet edeceği, `DMA_HandleTypeDef.Init.Request`
alanına yazılan bir sabitle (burada `DMA_REQUEST_ADC1`) yazılım
tarafından seçilir. Yani `DMA1_Channel1` yerine `DMA1_Channel3`
kullanmak isteseydik, tek değişiklik gereken yer kodun kendisiydi —
donanımsal bir kısıt yoktu.

## Karşılaşılan hata: donanımın tamamen kilitlenmesi ("hang")

İlk denemede kart, `HAL_ADC_Start_DMA()` çağrıldıktan hemen sonra
tamamen donuyordu — seri port sonsuza kadar sessiz kalıyordu.
Kullanıcının "seriale yazdırdığına emin misin?" sorusu üzerine
varsayımla ilerlemek yerine PowerShell üzerinden doğrudan COM portuna
bağlanılıp gerçek baytlar dinlendi; bu, açılış mesajının tam olarak
gittiğini ama `Start_DMA` sonrası **hiçbir** baytın gelmediğini
kesin olarak kanıtladı.

Kök neden: bu projede kullanılan `startup_stm32g474xx.S` dosyasının
kesme vektör tablosu, DMA1 kanal 1 kesmesini **`DMA1_CH1_IRQHandler`**
adıyla tanımlıyor — CubeMX'in bazı sürümlerinde/diğer örneklerde daha
sık görülen `DMA1_Channel1_IRQHandler` adı **değil**. Kodda yanlış
isim kullanılınca derleme ve bağlama (link) hatasız geçiyor, ama
yazılan fonksiyon gerçek vektör tablosuna hiç bağlanmıyor — o kesme
yuvası, varsayılan (weak) `Default_Handler`'a bağlı kalıyor ki bu da
sonsuz bir `b .` (kendi üzerine dallan) döngüsünden ibaret. İlk DMA
kesmesi (yarım tampon dolduğunda, `Start_DMA`'dan yaklaşık 100-200
mikrosaniye sonra) geldiği an işlemci bu sonsuz döngüye düşüyor ve bir
daha asla geri dönmüyor.

**Ders**: bir kesme handler'ı yazmadan önce, o STM32 ailesi/kart için
gerçekte kullanılan `startup_stmXXxx.S` dosyasındaki vektör tablosunu
kontrol edip **tam** sembol adını oradan almak gerekir — isimlendirme,
farklı ST kütüphane/CubeMX sürümleri arasında değişebiliyor
(`DMA1_CH1_IRQHandler` vs. `DMA1_Channel1_IRQHandler` gibi).

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
`RCC`, `GPIO`, `DMA`, `EXTI`, `FLASH`, `PWR`, `CORTEX`, `ADC` ve
`UART` modülleri. `Inc/stm32g4xx_hal_conf.h`, hangi HAL modüllerinin
derlemeye dahil edildiğini kontrol eden yapılandırma dosyasıdır.
