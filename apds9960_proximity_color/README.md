# apds9960_proximity_color

APDS-9960 (Adafruit breakout) ile **yakınlık (proximity)** ve **RGB/clear
ışık** verilerini I2C üzerinden okuyup USART2'ye akıtan örnek. Bu sensörün
sabit (değiştirilemeyen) 7-bit I2C adresi **0x39**'dur.

Gerçek donanımda doğrulandı:

```
I2C scan starting...
  found device at 0x39
I2C scan done.
APDS-9960 ID register: 0xAB (expected 0xAB)
Prox:   0   C:     0  R:     0  G:     0  B:     0
Prox: 180   C:   294  R:   154  G:    97  B:    81
Prox: 177   C:   293  R:   154  G:    97  B:    81
```

## Donanım bağlantısı

| Sinyal | Pin | Açıklama |
|---|---|---|
| APDS-9960 VIN | 3V3 | |
| APDS-9960 GND | GND | |
| APDS-9960 SCL | **PB8** | I2C1, AF4 |
| APDS-9960 SDA | **PB9** | I2C1, AF4 |
| USART2 TX/RX | PA2 / PA3 | ST-Link VCP, 115200 8N1 |

## MLX90614'ten çıkarılan ders: önce doğrula, sonra güven

Bu depodaki [MLX90614 denemesi](../) hiçbir zaman GitHub'a eklenemedi,
çünkü sensör (muhtemelen kusurlu bir klon) I2C hattına hiç cevap
vermedi — saatlerce "kod mu, kablo mu, sensör mü" diye debug edildi.
Bu projede aynı hataya düşmemek için **iki doğrulama adımı** en başa
eklendi, `apds9960_init()` çağrılmadan önce:

1. **I2C bus taraması** (`i2c_scan_and_report()`) — tüm 7-bit
   adresleri dener, hangisi ACK veriyorsa yazdırır. Cihaz bulunamazsa
   sorun kesinlikle donanımda demektir, kod tarafını hiç sorgulamaya
   gerek kalmaz.
2. **ID register kontrolü** (`0x92`) — APDS-9960 için beklenen değer
   `0xAB`'dir. Bu değer gelirse hem doğru adreste hem de gerçek/uyumlu
   bir çiple konuştuğunuzu bilirsiniz (bazı klonlar farklı bir ID
   döndürebilir ya da hiç cevap vermeyebilir).

Bu iki kontrol her ~3 saniyede bir tekrarlanır (`loop_count % 10`),
böylece terminali ne zaman açarsanız açın kaçırma ihtimaliniz olmaz.

## Çalışma mantığı

### Kayıt haritası (register map)

APDS-9960'ın register adresleri datasheet'te doğrudan `0x80` ve üzeri
değerler olarak tanımlıdır (bazı I2C cihazlarındaki gibi ayrıca bir
"komut biti" eklemeye gerek yoktur):

| Register | Adres | Açıklama |
|---|---|---|
| `ENABLE` | 0x80 | Güç ve hangi motorların (ALS/proximity/gesture) açık olduğu |
| `ATIME` | 0x81 | ALS (ışık) entegrasyon süresi |
| `PPULSE` | 0x8E | Proximity LED darbe sayısı/süresi |
| `CONTROL` | 0x8F | LED akımı, ALS/proximity kazancı (gain) |
| `ID` | 0x92 | Cihaz kimliği — APDS-9960 için sabit `0xAB` |
| `CDATAL` | 0x94 | Renk verisinin başlangıcı (8 bayt: clear/red/green/blue, her biri 16-bit little-endian) |
| `PDATA` | 0x9C | Ham proximity değeri (0-255, yakınlaştıkça artar) |

### Başlatma — `apds9960_init()`

1. `ENABLE = 0x00` ile her şeyi kapatarak temiz bir başlangıç yapılır.
2. `ATIME`, `PPULSE`, `CONTROL` register'larına makul varsayılan
   değerler yazılır (103ms ışık entegrasyonu, 8 adet 16µs'lik
   proximity darbesi, 100mA LED akımı, 4x kazanç).
3. Önce sadece `PON` (Power ON) biti set edilir, 10ms beklenir —
   sensörün dahili osilatörünün oturması için.
4. Ardından `PON | AEN | PEN` yazılarak hem ışık (ALS) hem de
   proximity ölçüm motorları aktif edilir.

### Okuma — `apds9960_read_all()`

`CDATAL`'den başlayarak **8 baytı tek seferde** okur
(`HAL_I2C_Mem_Read(..., buf, 8, ...)`) — bu, dört ayrı 16-bit rengi
(clear, red, green, blue) art arda gelen register'lardan tek bir I2C
işlemiyle çeker. Ardından `PDATA`'yı ayrı bir tek baytlık okuma ile
alır.

## Neler eklenmedi (kapsam dışı bırakılanlar)

- **Gesture (el hareketi) algılama**: APDS-9960'ın en "gösterişli"
  özelliği ama gerçekte GFIFO'dan çoklu foto-diyot verisi okuyup yön
  belirleyen bir algoritma gerektiriyor — bu proje kapsamının çok
  ötesinde, ayrı bir proje olarak ele alınabilir.
- **Kesme (interrupt) tabanlı okuma**: sensörün `INT` pini kullanılıp
  eşik değerleri aşıldığında EXTI ile haberdar olmak mümkün
  (`exti_button_led`'deki gibi), ama bu örnek basit tutmak için
  polling kullanıyor.

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
`RCC`, `GPIO`, `DMA`, `EXTI`, `FLASH`, `PWR`, `CORTEX`, `I2C` ve
`UART` modülleri. `Inc/stm32g4xx_hal_conf.h`, hangi HAL modüllerinin
derlemeye dahil edildiğini kontrol eden yapılandırma dosyasıdır.
