# uart_led_control

NUCLEO-G474RE ile PC arasında **UART üzerinden komutla haberleşme** örneği.
ST-Link'in sanal COM portu (USART2, 115200 8N1) üzerinden terminalden
gönderilen metin komutlarıyla kullanıcı LED'i (**LD2**) kontrol edilir ve
kart durumunu geri bildirir.

## Desteklenen komutlar

Terminalden bir satır gönderip Enter'a basınca (`\r`, `\n` veya `\r\n` ile biter):

| Komut | Kartın cevabı |
|---|---|
| `LED ON` | LED yanar, `OK` döner |
| `LED OFF` | LED söner, `OK` döner |
| `STATUS` | `LED: ON/OFF`, `Temperature: 24.5 C`, `ADC: 1823` satırlarını döner |
| (bilinmeyen) | `ERR: unknown command` döner |

> **Not:** `STATUS` çıktısındaki `Temperature` ve `ADC` değerleri şimdilik
> **sabit placeholder** değerlerdir. Gerçek dahili sıcaklık sensörü / ADC
> okuması, bu projenin öğrenme hedefi olan UART + interrupt + string
> parsing'den ayrı, ilerideki bir "ADC" dersinin konusudur.

## Terminalden test etme

Herhangi bir seri terminal programıyla (PuTTY, Tera Term, `screen`, VS Code
Serial Monitor eklentisi vb.) kartın ST-Link COM portuna **115200 8N1**
ayarıyla bağlanıp yukarıdaki komutları yazmanız yeterli. PowerShell'den de
hızlıca test edilebilir:

```powershell
$port = New-Object System.IO.Ports.SerialPort COM21,115200,None,8,One
$port.Open()
$port.Write("LED ON`r`n")
Start-Sleep -Milliseconds 200
$port.ReadExisting()
$port.Close()
```

(`COM21` yerine kendi ST-Link portunuzun numarasını kullanın — Aygıt
Yöneticisi'nde "STMicroelectronics STLink Virtual COM Port" olarak görünür.)

## Pin bağlantıları

| Sinyal | Pin | Açıklama |
|---|---|---|
| LD2 (kullanıcı LED'i) | PA5 | Output |
| USART2 TX | PA2 | ST-Link VCP'ye bağlı, AF7 |
| USART2 RX | PA3 | ST-Link VCP'den gelir, AF7 |

PA2/PA3, NUCLEO-64 kartlarında ST-Link'in USB-seri köprüsüne donanımsal
olarak bağlıdır — ekstra bir USB-seri adaptöre gerek yoktur, kartın kendi
USB kablosu hem programlama hem seri haberleşme için yeterlidir.

## Çalışma mantığı

```
main() → HAL_Init() → GPIO+USART2 kurulum → HAL_UART_Receive_IT(1 bayt)
                                                        │
                                        her bayt geldiğinde donanım kesmesi
                                                        ▼
                                  USART2_IRQHandler() → HAL_UART_IRQHandler()
                                                        ▼
                                          HAL_UART_RxCpltCallback()
                                     (baytı rx_line'a ekle, satır bitince
                                      line_ready=1 yap, sonraki bayt için
                                      tekrar HAL_UART_Receive_IT çağır)
                                                        │
                     main() döngüsü line_ready'yi görünce process_command() çağırır
```

### Neden tek seferde tüm satırı almıyoruz?

`HAL_UART_Receive_IT(&huart2, &rx_byte, 1)` her çağrıldığında **sadece 1
bayt** bekleyip geldiğinde kesme üretir — komutun kaç karakter olacağı
önceden bilinmediği için (kullanıcı ne yazacak, bilinmiyor) sabit uzunluklu
bir alım yapılamaz. Bunun yerine klasik bir **RX buffer + string parsing**
deseni kullanılır: her bayt `rx_line[]` dizisine eklenir, `\r` veya `\n`
görülünce satır tamamlanmış sayılır.

### Kod parçaları — [main.c](Src/main.c)

- **`MX_USART2_UART_Init()`** — `huart2` struct'ını 115200 baud, 8N1 ile
  doldurup `HAL_UART_Init()` çağırır, ardından `HAL_NVIC_EnableIRQ(USART2_IRQn)`
  ile kesmeyi açar.
- **`HAL_UART_Receive_IT(&huart2, &rx_byte, 1)`** — "1 bayt gelince beni
  kesmeyle haberdar et" der. **Tek seferliktir**: her bayt işlendikten sonra
  yeniden çağrılması gerekir, yoksa bir sonraki bayt için dinleme durur.
- **`USART2_IRQHandler(void)`** — vektör tablosundaki gerçek kesme
  fonksiyonu; işi doğrudan `HAL_UART_IRQHandler(&huart2)`'e devreder.
- **`HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)`** — HAL'de weak
  tanımlı, burada override edilir. Gelen bayt `\r`/`\n` değilse buffer'a
  eklenir; satır sonuysa `rx_line` sonlandırılıp `line_ready` bayrağı
  set edilir ve **hemen ardından** bir sonraki bayt için
  `HAL_UART_Receive_IT` tekrar çağrılır (bu adım unutulursa alım bir
  daha tetiklenmez).
- **`process_command(const char *cmd)`** — `strcmp()` ile komutu
  karşılaştırıp GPIO'yu değiştirir ve `uart_send()` (içeride
  `HAL_UART_Transmit()`) ile cevap gönderir. `STATUS` cevabı
  `snprintf()` ile tek bir buffer'da biçimlendirilip tek seferde yollanır.
- **`SysTick_Handler()`** — `HAL_UART_Transmit()`'in `HAL_MAX_DELAY`
  zaman aşımı kontrolü dahil, HAL'in iç zamanlama mekanizması
  `HAL_GetTick()`'e dayandığından burada da gereklidir.

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
`CORTEX` ve `UART` modülleri. `Inc/stm32g4xx_hal_conf.h`, hangi HAL
modüllerinin derlemeye dahil edildiğini kontrol eden yapılandırma dosyasıdır.
