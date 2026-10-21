// Content of the guide. Regions are in pixels of the radio screen (160x128): x, y, width, height.
export type Region = readonly [number, number, number, number];

export type GuideItem = {
  label: string;
  text: string;
  region?: Region;
  image?: string; // screen displayed from this item on
  code?: string; // terminal scenes
};

export type GuideSceneData = {
  title: string;
  image?: string;
  terminal?: boolean;
  items: GuideItem[];
};

export const CALLSIGN = "TA2TWC";
export const BOOT_FRAMES = 61;
export const SHUTDOWN_FRAMES = 31;
export const SCREEN_FRAME_MS = 40;

export const scenes: GuideSceneData[] = [
  {
    title: "Ana ekran",
    image: "home_live",
    items: [
      { label: "Durum çubuğu", text: "Mod, bant genişliği (DMR'de TS ve CC), güç, pil yüzdesi", region: [0, 0, 160, 14] },
      { label: "Kanal adı", text: "Seçili kanalın adı", region: [0, 14, 160, 16] },
      { label: "Frekans", text: "Alış frekansı; gönderirken kırmızıya döner", region: [0, 30, 160, 32] },
      { label: "Bölge ve TX", text: "Bölge, kanal sırası ve gönderme frekansı", region: [0, 62, 160, 11] },
      { label: "Mod bilgisi", text: "FM'de ton ve susturucu, DMR'de konuşma grubu", region: [0, 73, 160, 17] },
      { label: "Sinyal", text: "S-metre, kalite, dBm, pil voltajı, sıcaklık", region: [0, 90, 160, 27] },
      { label: "Geçmiş", text: "Sinyal çizelgesi; yeni değer sağdan girer", region: [0, 117, 160, 11] },
    ],
  },
  {
    title: "Sinyal değerleri",
    image: "home_dmr",
    items: [
      { label: "S değeri", text: "S0 ile S9 arası; üstü S9+10 biçiminde", region: [0, 89, 40, 11] },
      { label: "Q kalite", text: "%0 yalnızca gürültü, %100 temiz sinyal", region: [52, 89, 46, 11] },
      { label: "dBm", text: "Alınan sinyalin gücü", region: [104, 89, 56, 11] },
      { label: "G gürültü", text: "Ham gürültü değeri; düşük olması iyidir", region: [0, 106, 26, 11] },
    ],
  },
  {
    title: "DMR çağrısı",
    image: "call",
    items: [
      { label: "Konuşma grubu", text: "Çağrının geldiği TG", region: [0, 14, 160, 16] },
      { label: "Çağrı işareti", text: "Konuşan istasyon, büyük yazıyla", region: [0, 30, 160, 32] },
      { label: "Ad ve yer", text: "Kimlik veritabanındaki ad, şehir, lokator", region: [0, 62, 160, 11] },
      { label: "DMR kimliği", text: "Kimlik numarası ve frekans", region: [0, 73, 160, 17] },
    ],
  },
  {
    title: "Gönderme",
    image: "tx",
    items: [
      { label: "TX rozeti", text: "Durum çubuğunda kırmızı TX yazar", region: [0, 0, 36, 14] },
      { label: "Frekans", text: "Gönderme frekansı kırmızı gösterilir", region: [0, 30, 160, 32] },
      { label: "Süre", text: "Zaman aşımı açıksa kalan süre", region: [112, 73, 48, 17] },
      { label: "Süre çubuğu", text: "Kalan süreyi gösterir; FM'de altında mikrofon seviyesi", region: [0, 91, 160, 9] },
      { label: "Değerler", text: "Güç, pil voltajı ve sıcaklık", region: [0, 105, 160, 12] },
    ],
  },
  {
    title: "Menüler",
    image: "menu_main",
    items: [
      { label: "Yeşil tuş", text: "Menüyü açar, seçimi onaylar" },
      { label: "9 satır", text: "Küçük yazıyla daha çok seçenek görünür" },
      { label: "Ekran Seç.", text: "Menü → Seçenekler → Ekran Seç.", image: "menu_display" },
      { label: "Ana ekran", text: "Yeni tasarım ile klasik görünüm arasında geçiş" },
      { label: "Ses göstergesi", text: "Açıkken düğmeyi çevirince ses çubuğu görünür", image: "volume" },
      { label: "Kaydetme", text: "Yeşil tuş kaydeder, kırmızı tuş iptal eder" },
    ],
  },
  {
    title: "Diğer ekranlar",
    image: "rssi",
    items: [
      { label: "RSSI", text: "İbreli S-metre ve tepe değeri işareti" },
      { label: "Son Duyulanlar", text: "Çağrı işareti, geçen süre, sinyal gücü", image: "last_heard" },
      { label: "Spektrum", text: "VFO modunda # tuşuna uzun bas: spektrum ve şelale", image: "sweep" },
    ],
  },
  {
    title: "Yazılım yükleme",
    terminal: true,
    items: [
      { label: "Yedek al", text: "Telsiz açık ve USB'ye bağlıyken", code: "python tools/radio.py backup" },
      { label: "İmajı hazırla", text: "Derle, ses kodeğini ve dili ekle", code: "python build.py && python tools/make_image.py" },
      { label: "Yükleme modu", text: "PTT ve üstündeki tuş basılıyken telsizi aç" },
      { label: "Yükle", text: "Yaklaşık 20 saniye sürer", code: "python tools/flash.py release/TA2TWC_MDUV390_xxxxxxx.bin" },
      { label: "Geri dönüş", text: "Yedekteki eski yazılım aynı yolla yüklenir", code: "python tools/flash.py backup/<tarih>/restore_firmware.bin" },
    ],
  },
];
