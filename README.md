# OpenGD77 · TA2TWC ekranları

OpenGD77 tabanlı, yeni ekranlarla gelen gayriresmî bir yazılım. Desteklenen telsizler:

- **TYT MD-UV380 / MD-UV390** (Retevis RT-3S)
- **Baofeng DM-1701** (Retevis RT-84)
- **Radioddity GD-77**

Telsizin radyo tarafı (alma, gönderme, DMR, kanal ve kişi yapısı) OpenGD77 ile aynıdır. Değişen, ekranda gördükleriniz ve birkaç kullanım kolaylığıdır.

> **Resmî OpenGD77 değildir.** Hiçbir garanti verilmez; yükleme ve kullanım sorumluluğu kullanıcıya aittir.
> Telsizi yalnızca lisansınızın izin verdiği frekans ve güçlerde kullanın.

![MD-UV390 ana ekran](docs/img/home_live.png)

---

## Neler var

### TYT MD-UV380 / MD-UV390 ve Baofeng DM-1701 (renkli ekran)

| | |
|---|---|
| **Yeni ana ekran** | Büyük frekans, kanal adı, bölge, TX frekansı ve teknik değerler tek ekranda. İstenirse menüden klasik görünüme dönülür. |
| **Sinyal göstergeleri** | S değeri, dBm, sinyal kalitesi (Q %), gürültü, pil voltajı, sıcaklık; altta kayan sinyal geçmişi. |
| **DMR çağrı ekranı** | Çağrı işareti büyük yazıyla; ad, şehir, DMR kimliği ve konuşma grubu ayrı satırlarda. |
| **Gönderme ekranı** | TX rozeti, kalan süre çubuğu, FM'de mikrofon seviyesi. |
| **Spektrum ve şelale** | VFO taramasında renkli spektrum ve zaman içinde akan şelale. |
| **İbreli S-metre** | RSSI ekranında tepe değeri işaretli analog gösterge. |
| **Açılış ve kapanış** | Telsize kayıtlı çağrı işaretinin ve DMR kimliğinin göründüğü animasyonlar. |
| **Diğer** | Pil yüzdesi, 9 satırlı menü, koyu tema, Türkçe ve İngilizce yazılar. |

DM-1701 için ekranlar 160×120 ekrana göre ayrıca düzenlendi.

<p>
<img src="docs/img/tx.png" width="24%"> <img src="docs/img/sweep.png" width="24%"> <img src="docs/img/rssi.png" width="24%"> <img src="docs/img/call.png" width="24%">
</p>

### Radioddity GD-77 (128×64 siyah-beyaz ekran)

Aynı tasarımın GD-77'nin küçük ekranına uyarlanmış hali:

- açılış ve kapanış animasyonları;
- büyük kanal adı ve frekansla kanal ekranı;
- sinyal yokken bölge adı, sinyal gelince S-metre;
- dev yazılı çağrı ekranı;
- ibreli S-metre;
- spektrumun altında şelale;
- 5 satırlı son duyulanlar listesi.

![GD-77 ekranları](docs/img/gd77/0_genel_bakis.png)

GD-77'nin program belleği neredeyse dolu. Yeni ekranlara yer açmak için bu yapıda **GPS ve hotspot modu kaldırıldı** (GD-77'de GPS alıcısı zaten yok). VFO ana ekranı OpenGD77'nin özgün görünümündedir.

GD-77 görüntüleri, yazılımın ekran düzeniyle aynı yazı tipleri kullanılarak bilgisayarda çizilmiştir (`sim/gd77_mockup.py`).

---

## Ne kadar denendi

| Telsiz | Durum |
|---|---|
| TYT MD-UV390, 5 W | Üç telsizde kullanılıyor. |
| TYT MD-UV390 Plus 10 W | Derleniyor, **denenmedi**. |
| Baofeng DM-1701 | Bir telsizde daha eski bir sürüm denendi. Bu sürüm **denenmedi**. |
| Retevis RT-84 | Derleniyor, **denenmedi**. |
| Radioddity GD-77 | Bir telsize yüklendi, ekranlar çalışıyor. Gönderme sesi henüz doğrulanmadı (aşağıdaki nota bakın). |

---

## DMR ses kodeği hakkında

DMR için gereken AMBE ses kodeği lisanslı bir yazılımdır. **Bu depoda yoktur ve derlenen dosyalara da konmaz.** Resmî OpenGD77 dağıtımında olduğu gibi, kodeği yükleme sırasında OpenGD77 CPS ekler. Bunun için kullanıcının kendi indirdiği resmî üretici yazılımı (donör) kullanılır.

Kodek içeren yükleme dosyalarını (örneğin `release/` klasöründekileri) veya telsizden alınmış yedekleri paylaşmayın.

---

## Kurulum

En kolay yol OpenGD77 CPS'nin yükleyicisidir: **Extras → Firmware loader**. Adım adım anlatım:
[docs/OpenMDUV390_TA2TWC_Kurulum_Kilavuzu.pdf](docs/OpenMDUV390_TA2TWC_Kurulum_Kilavuzu.pdf)

- **MD-UV380/390 ve DM-1701:** Donör, TYT MD-9600 resmî yazılımıdır (`MD9600-CSV(2571V5)-V26.45.bin`). CPS bu dosyayı içeriğinden doğrular.
- **GD-77:** Donör, Radioddity GD-77 resmî yazılımı v4.3.6'dır.

Kurulumdan önce mutlaka yedek alın. Yedek ile eski yazılıma her zaman dönülebilir.

---

## Derleme

Windows'ta, Python 3 ile.

### MD-UV380 / MD-UV390 / DM-1701

[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (arm-none-eabi, 14.2 ile denendi) gerekir.

```
python build.py                         # MD-UV380 / MD-UV390 (5 W)
python build.py -c MDUV380_10W_PLUS_FW  # MD-UV390 Plus 10 W
python build.py -c DM1701_FW            # Baofeng DM-1701
python build.py -c RT84_FW              # Retevis RT-84
python tools/make_release.py            # kodeksiz, CPS ile yüklenecek paket
```

### GD-77

OpenGD77 MK22 kaynakları **MCUXpresso v24.12 veya daha eski** bir derleyici ister; yenileri gönderme sesini bozuyor. Bu depo IDE olmadan derler:

- derleyici: Arm GNU Toolchain **13.2.rel1**;
- C kütüphanesi: newlib-nano (MCUXpresso'nun Redlib'i IDE dışında dağıtılmıyor).

```
python tools/build_gd77.py
```

> **GD-77 notu:** Bu derleme, OpenGD77'nin resmî derleme ortamından farklıdır. Kullanmadan önce DMR ve FM gönderme sesini mutlaka deneyin.

---

## Klasörler

| Klasör | İçerik |
|---|---|
| `MDUV380_firmware/` | MD-UV380/390, DM-1701, RT-84 yazılım kaynakları (OpenGD77 R20260131 + yeni ekranlar) |
| `GD77_firmware/` | GD-77 yazılım kaynakları (OpenGD77 MK22 R20260131 + yeni ekranlar) |
| `tools/` | Derleme, paketleme, yedek, ekran görüntüsü, uydu listesi, konum, tema araçları |
| `sim/` | Ekranları bilgisayarda görmek için önizleme araçları |
| `docs/` | Kılavuz, broşür, görüntüler, Türkiye kanal listesi |
| `data/` | Türkiye röle listesi (DARD81) ve uydu listesi kaynakları |
| `upstream/` | Değiştirilmemiş resmî OpenGD77 dosyaları |

---

## Lisans ve emeği geçenler

Bu proje [OpenGD77](https://www.opengd77.com) üzerine kuruludur:

- Roger Clark VK3KYY, Daniel Caujolle-Bert F1RMB, Kai DG4KLU, Colin G4EML, Alex DL4LEX ve katkıda bulunanlar;
- OpenGD77'nin özgün README'si: [README_OpenGD77.md](README_OpenGD77.md).

OpenGD77 lisansı ([license.txt](license.txt)) değiştirmeye ve paylaşmaya izin verir, ancak **ticari kullanımı yasaklar**: yazılım satılamaz, yazılım yüklenmiş cihaz bu yazılım gerekçesiyle ücretlendirilemez. Kaynak dosyalardaki telif bildirimleri korunmalıdır.

Eklenen ekranlar ve araçlar: Copyright (C) 2026 TA2TWC, aynı lisansla.

73 · **TA2TWC**

---

### English summary

Unofficial OpenGD77 build with redesigned screens for the TYT MD-UV380/390, Baofeng DM-1701 / Retevis RT-84 and Radioddity GD-77:

- new home screen;
- signal meters and analog S-meter;
- spectrum with waterfall;
- large callsign caller screen;
- boot and shutdown animations.

The AMBE codec is **not** included: the OpenGD77 CPS firmware loader merges it from the official donor firmware, as for the official releases.

On the GD-77, GPS and hotspot mode were removed to make room. The GD-77 build uses GCC 13.2 with newlib-nano instead of MCUXpresso: test the transmit audio before use.

License: OpenGD77 license, non-commercial use only. Credits to the OpenGD77 developers.
