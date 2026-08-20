# STM32_NucleoG474

![NUCLEO-G474RE](Nucleo-G474RE-Board.png)

NUCLEO-G474RE geliştirme kartı için hazırlanan örnek projelerin tutulduğu depo.

Her örnek kendi klasöründe, bağımsız ve derlenebilir bir proje olarak yer alır.
Yeni bir örnek eklendikçe bu listeye yeni bir satır eklenir.

## Kart

- **MCU:** STM32G474RET6 (Arm Cortex-M4)
- **Board:** NUCLEO-G474RE
- Tüm örnekler gerçek STM32 CMSIS/HAL kütüphanesini kullanır (`Drivers/CMSIS`, `Drivers/STM32G4xx_HAL_Driver`); her klasör kendi minimal HAL modül alt kümesiyle bağımsız olarak derlenir.

## Klasörler

| Klasör | Açıklama |
|---|---|
| [`buton_on_off/`](buton_on_off) | Kullanıcı butonuna (B1) basılı tutulduğunda kullanıcı LED'ini (LD2) yakan, bırakınca söndüren örnek (`HAL_GPIO_ReadPin`/`WritePin`) |
| [`buton_toggle/`](buton_toggle) | Kullanıcı butonuna (B1) her basıldığında kullanıcı LED'inin (LD2) durumunu tersine çeviren (toggle) örnek (`HAL_GPIO_TogglePin`, debounce için `HAL_Delay`) |
| [`timer_blink/`](timer_blink) | `HAL_Delay()` kullanmadan, TIM3 donanım zamanlayıcısının kesmesiyle (interrupt) her 500 ms'de bir LED yakıp söndüren örnek (`HAL_TIM_Base_Start_IT`, `HAL_TIM_PeriodElapsedCallback`) |
| [`uart_led_control/`](uart_led_control) | PC'den terminal üzerinden (USART2, ST-Link VCP) `LED ON` / `LED OFF` / `STATUS` komutlarıyla kartı kontrol eden örnek (`HAL_UART_Receive_IT`, RX buffer + string parsing) |
| [`adc_pwm_dimmer/`](adc_pwm_dimmer) | Potansiyometre (ADC, PA0) ile LED parlaklığını (PWM, TIM2/PA5) analog kontrol eden örnek; anlık ADC/duty değerleri USART2 üzerinden seri terminale de akar |
| [`ntc_temperature/`](ntc_temperature) | NTC termistör (ADC, PA0 + gerilim bölücü) ile ortam sıcaklığını okuyup USART2 üzerinden seri terminale akıtan örnek; Beta denklemi `log()` yerine önceden hesaplanmış bir lookup table ile çözülür (bkz. README'deki toolchain notu) |

## Ortak build/flash yöntemi

Tüm klasörler STM32CubeCLT / STM32Cube for VS Code eklentisiyle gelen `cube-cmake` ve `cube` araçlarını kullanır:

```bash
cd <klasör_adı>
cube-cmake --preset Debug
cube-cmake --build --preset Debug
cube programmer -c port=SWD -w build/Debug/<proje>.elf -v -rst
```

ST-Link kartın üzerinde entegre olduğu için ekstra bir programlayıcıya gerek yoktur; USB ile PC'ye bağlamak yeterlidir.
