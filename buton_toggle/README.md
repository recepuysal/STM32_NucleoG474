# buton_toggle

NUCLEO-G474RE kartında kullanıcı butonuna (**B1**) her basıldığında kullanıcı
LED'inin (**LD2**) durumunu tersine çeviren örnek: basınca yak, tekrar basınca
söndür. [buton_on_off](../buton_on_off) örneğinden farkı, LED'in butona basılı
kalma süresine değil, sadece basma **anına** tepki vermesidir.

CMSIS/HAL kütüphanesi kullanılmamıştır; RCC ve GPIO register'larına
[main.c](Src/main.c) içinde doğrudan adres üzerinden erişilir.

## Pin bağlantıları

| Sinyal | Pin | Açıklama |
|---|---|---|
| LD2 (kullanıcı LED'i) | PA5 | Output, HIGH = LED yanık |
| B1 (kullanıcı butonu) | PC13 | Input, dahili pull-down; basılınca HIGH okunur |

> Not: Bu karttaki B1 butonu basıldığında PC13'ü VDD'ye (HIGH) çeker, bu yüzden
> boşta kararlı LOW okumak için yazılımsal pull-down (`PUPDR`) kullanılmıştır.

## Çalışma mantığı

Her buton basımı, döngünün milyonlarca kez çalıştığı süre boyunca "basılı"
olarak defalarca okunur. Sadece basılı/basılı-değil geçişinde (**rising
edge**) bir kez toggle yapmak için önceki tur ile şimdiki turun durumu
karşılaştırılır:

1. `was_pressed` bir önceki döngüdeki buton durumunu tutar.
2. `pressed_now && !was_pressed` şartı, "az önce basılı değildi, şimdi basılı"
   geçişini, yani butona **yeni basıldığı** anı yakalar.
3. Bu an yakalanınca kısa bir **debounce** gecikmesi (`delay(50000)`)
   uygulanır; mekanik buton kontakları basıldığı anda birkaç kez çok hızlı
   açılıp kapanabildiğinden (sıçrama), bu bekleme sahte çoklu geçişleri
   eler.
4. Bekleme sonunda buton hâlâ basılıysa gerçek bir basmadır ve
   `GPIOA_ODR ^= (1 << PA5)` ile PA5 biti **XOR**'lanarak LED'in durumu
   tersine çevrilir.
5. `was_pressed` güncellenir ve döngü buton bırakılana kadar (ve sonraki
   basışa kadar) tekrar tetiklenmez.

## Derleme ve yükleme

STM32Cube for VS Code eklentisiyle gelen `cube-cmake` ve `cube` araçları kullanılır.

```bash
cube-cmake --preset Debug
cube-cmake --build --preset Debug
cube programmer -c port=SWD -w build/Debug/STM32G474RE.elf -v -rst
```

Kart, ST-Link programlayıcısı üzerinden USB ile bilgisayara bağlı olmalıdır.
