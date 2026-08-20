# buton_on_off

NUCLEO-G474RE kartında kullanıcı butonuna (**B1**) basılı tutulduğu sürece
kullanıcı LED'ini (**LD2**) yakan, bırakılınca söndüren basit bir örnek.

CMSIS/HAL kütüphanesi kullanılmamıştır; RCC ve GPIO register'larına
[main.c](Src/main.c) içinde doğrudan adres üzerinden erişilir.

## Pin bağlantıları

| Sinyal | Pin | Açıklama |
|---|---|---|
| LD2 (kullanıcı LED'i) | PA5 | Output, HIGH = LED yanık |
| B1 (kullanıcı butonu) | PC13 | Input, dahili pull-down; basılınca HIGH okunur |

> Not: Bu karttaki B1 butonu basıldığında PC13'ü VDD'ye (HIGH) çeker.
> Bu yüzden boşta kararlı LOW okumak için yazılımsal pull-down (`PUPDR`)
> kullanılmıştır.

## Çalışma mantığı

`main.c` sonsuz döngüde PC13'ü okur:
- **HIGH** (buton basılı) → PA5 HIGH → LED yanar
- **LOW** (buton bırakılmış) → PA5 LOW → LED söner

## Derleme ve yükleme

STM32Cube for VS Code eklentisiyle gelen `cube-cmake` ve `cube` araçları kullanılır.

```bash
cube-cmake --preset Debug
cube-cmake --build --preset Debug
cube programmer -c port=SWD -w build/Debug/STM32G474RE.elf -v -rst
```

Kart, ST-Link programlayıcısı üzerinden USB ile bilgisayara bağlı olmalıdır.
