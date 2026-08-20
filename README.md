# STM32_NucleoG474

NUCLEO-G474RE geliştirme kartı için hazırlanan örnek projelerin tutulduğu depo.

Her örnek kendi klasöründe, bağımsız ve derlenebilir bir proje olarak yer alır.
Yeni bir örnek eklendikçe bu listeye yeni bir satır eklenir.

## Kart

- **MCU:** STM32G474RET6 (Arm Cortex-M4)
- **Board:** NUCLEO-G474RE
- İlk örnekler (`buton_on_off`, `buton_toggle`) CMSIS/HAL kütüphanesi kullanmadan, doğrudan register erişimiyle (bare-metal) yazılmıştır. `timer_blink`'ten itibaren gerçek STM32 CMSIS/HAL kütüphanesi kullanılmaktadır — her klasörün kendi README'sinde hangi yaklaşımın izlendiği belirtilir.

## Klasörler

| Klasör | Açıklama |
|---|---|
| [`buton_on_off/`](buton_on_off) | Kullanıcı butonuna (B1) basılı tutulduğunda kullanıcı LED'ini (LD2) yakan, bırakınca söndüren örnek |
| [`buton_toggle/`](buton_toggle) | Kullanıcı butonuna (B1) her basıldığında kullanıcı LED'inin (LD2) durumunu tersine çeviren (toggle) örnek |
| [`timer_blink/`](timer_blink) | `HAL_Delay()` kullanmadan, TIM3 donanım zamanlayıcısının kesmesiyle (interrupt) her 500 ms'de bir LED yakıp söndüren örnek — gerçek STM32 HAL kütüphanesini kullanan ilk proje |

## Ortak build/flash yöntemi

Tüm klasörler STM32CubeCLT / STM32Cube for VS Code eklentisiyle gelen `cube-cmake` ve `cube` araçlarını kullanır:

```bash
cd <klasör_adı>
cube-cmake --preset Debug
cube-cmake --build --preset Debug
cube programmer -c port=SWD -w build/Debug/<proje>.elf -v -rst
```

ST-Link kartın üzerinde entegre olduğu için ekstra bir programlayıcıya gerek yoktur; USB ile PC'ye bağlamak yeterlidir.
