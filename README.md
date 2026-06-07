# OTACoojaSimulasyon
İşletim Sistemleri Dönem Sonu Projesi

YouTube bağlantısı: https://youtu.be/q966e-f6NZ0

FIRMWARE DOSYASINI GÖNDERİCİ DÜĞÜMÜN DOSYA SİSTEMİNE YÜKLEME

Göderici düğüm için "cooja mote" düğüm tipi tercih edilmiştir. Çünkü gönderilecek firmware dosyası düğüm firmware kodunun içine, parçalansa dahi, import edilememekte ve "ROM overflow" hatası vermektedir. Cooja script arayüzü kullanılarak yapılan denemelerde de ise önceden kullanılan bazı fonksiyonların artık tanımlı olmaması gibi nedenlerden ötürü dosyanın yüklenmesinde başarılı olunamamıştır. Sadece yeni firmware dosyasının, okunmak üzere gönderici düğümün dosya sistemine yazılmasında sağladığı esneklik nedeniyle bu tercih yapılmıştır. "xxd -i new-firmware.z1 > new-firmware.h" komutu ile yeni firmware verilşerini içeren "new-firmware.h" dosyası oluşturularak içeri alınmış ve "cooja mote"un sağladığı geniş alan sayesinde tek seferde dosya sistemine "new-firmware.z1" olarak kaydedilmiştir.


KULLANILAN VERİ YAPILARI

Gönderilen ve alınan paketler için tanımlanan veri yapıları şu şekildedir:

```
#define VERI_BOYUTU 64

// Paket veri yapısı tanımlaması
struct __attribute__((packed)) firmware_paket {
    uint16_t paket_num;
    uint16_t toplam_paket_sayisi;

    uint8_t veri[VERI_BOYUTU];
    uint16_t uzunluk; // Veri uzunluğu

    uint16_t checksum; // Paket bütünlüğü kontrolü için toplam değişkeni
    uint32_t ofset; // Flaşta yazılacağı adres

    uint8_t paket_tipi; // 0: veri paketi 1: başlangıç paketi 2: bitiş paketi
    uint32_t ek_veri; // Paket tipine bağlı ek veriler göndermek için değişken
};

// Paketin alındığına dair ACK veri yapısı tanımı
struct __attribute__((packed)) ack_paket {
    uint16_t paket_num;
    uint8_t durum; // 0: başarılı 1: bozuk (tekrar gönder) 2: yazma hatası
};
```

İstemciden sunucuya gönderilen, firmware verilerinin paketler halinde iletilmesini sağlayan "firmware_paket" yapısıdır. Burada "paket_num" ve "toplam_paket_sayisi" ilgili paketin numarasını ve gönderilecek toplam paket sayısını tutarken "veri[]" ise asıl veriyi taşıyan dizi işaretçisidir (PAYLOAD). Paket boyutu, Contiki-NG paket boyutu sınırını aşıp alt katmanlardan parçalara bölünmemesi için 64 byte olarak belirlenmiştir. Ayrıca "uzunluk" ile paketle gelen verinin boyutu, "checksum" ile istemci tarafından CRC-16 ile hesaplanan sağlama toplama değeri, "ofset" ile verinin sunucu flaşında yazılacağı dosya ofset değeri de taşınmaktadır. "paket_tipi", sunucunun paket tipine göre işlem yapmasını sağlamaktadır. Örneğin ilk pakette dosya belirtecinin tanımlanması, son pakette dosyanın kapatılarak dosya bütünlüğünün doğrulanması. "ek_veri" de yukarıdakilere ek verilerin gönderilmesi gerektiği durumlar için tanımlanmıştır. İlk pakette toplam dosya boyutu sunucuya gönderilerek sunucunun hazırlık yapması (Flaşta dosya boyutu kadar sıfırlardan oluşan boş dosya oluşturulması planlanmış ancak depolama alanı açısından verimsiz olduğu görülmüştür.), son pakette CRC-32 ile istemci tarafından hesaplanmış dosya sağlama toplamı gönderilerek sunucunun dosya bütünlüğünü doğrulaması amaçlanmaktadır.

Sunucudan istemciye gönderilen ve alınan firmware paketinin durumunu belirten "ack_paket" yapısıdır. Bu yapıyla paket numarasına ek olarak paket başarıyla alınıp kaydedildiği ve paketin istemci tarafından tekrar gönderilmesini sağlayan paket doğrulama hatası (durum 1) ve yazma hatası (durum 2) durumlarının istemciye bildirilmesi amaçlanmaktadır.


PAKETLERİN HAZIRLANMASI ve GÖNDERİLMESİ

İstemci tarafında paketin hazırlanamsı şu şekildedir:

```
      pkt.ofset = (uint32_t)suanki_paket_sirasi * VERI_BOYUTU;

      /* "new-firmware.z1" Dosyasını okuma */
      cfs_seek(fd, pkt.ofset, CFS_SEEK_SET);
      int okunan = cfs_read(fd, pkt.veri, VERI_BOYUTU);

      if (okunan > 0) {

        /* Paketi hazırlama */
        if (suanki_paket_sirasi == 0) {
          pkt.paket_tipi = 1;
          // Ek işlem yapmak gerekebilir diye ilk pakete toplam dosya boyutu eklenir.
          pkt.ek_veri = dosya_boyutu;
        } else if (suanki_paket_sirasi == toplam_paket_sayisi - 1) {
          pkt.paket_tipi = 2;
          // Son pakate dosya CRC-32 sağlama toplamı eklenir.
          pkt.ek_veri = dosya_toplam_crc;
        } else {
          pkt.paket_tipi = 0;
          pkt.ek_veri = 0;
        }

        pkt.paket_num = suanki_paket_sirasi;
        pkt.uzunluk = (suanki_paket_sirasi == toplam_paket_sayisi - 1) ? (dosya_boyutu % VERI_BOYUTU) : VERI_BOYUTU;
        pkt.checksum = checksum_hesapla(pkt.veri, pkt.uzunluk);
```

Öncelikle verinin ofseti belirlenerek pakete eklenmektedir. Sonrasında Coffee dosya sisteminde hazır bulunan "new-firmware.z1" dosyası en fazla "VERI_BOYUTU" = 64 byte okunarak paketin veri (PAYLOAD) kısmına yazılmaktadır. Paket tipi, ek veri ve paket numarası değişkenleri mevcut paket sırasına göre belirlenmektedir. Paketteki veri uzunluğu paket sırası, dosya boyutu ve veri boyutuna göre belirlenmektedir. En son CRC-16'ya göre paketteki verinin sağlama toplam değeri alınarak pakete eklenmektedir. Bu şekilde paket hazırlığı tamamlanmaktadır. Oluşturulan paket şu şekilde sunucu düğüme iletilmektedir:

```
// ACK bekleme süresi tanımlaması
#define TIMEOUT (10 * CLOCK_SECOND)

...
        
        /* Paketi gönderme ve zamanlayıcı başlatma */
        simple_udp_sendto(&udp_conn, &pkt, sizeof(pkt), &dest_ipaddr);
        etimer_set(&timeout_timer, TIMEOUT);

        /* Bekleme (Ya ACK gelecek ya da zaman aşımı olacak) */
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE || etimer_expired(&timeout_timer));
```

Paket gönderildikten sonra ACK paketinin gelmesi beklenmektedir. Eğer ACK paketi gelmediyse veya giden paket bozuk ise döngünün başına dönülmekte ve paket tekrar hazırlanarak gönderilmektedir. Simülasyonda paket kaybının olmadığı varsayılarak paketi yeniden hazırlama işlemlerinin atlanma mekanizması eklenmemiştir.

Buradan anlaşılacağı gibi projede dur ve bekle mekanizması kullanılmıştır. Sıradaki paket gönderilmekte ve olumlu ACK paketi gelene kadar aynı paket gönderilmeye devam edilmektedir.


PAKETLERİN ALINMASI ve KAYDEDİLMESİ

Sunucu düğümde herhangi bir dosya kısıtlamasıyla karşılaşılmadığı için "z1 mote" tipinde belirlenmiştir. İstemciden gelen ilk firmware paketiyle
```write_fd = cfs_open("new-firmware.z1", CFS_READ | CFS_WRITE);```
koduyla dosya belirteci açılmaktadır. Daha sonra şu şekilde paketler flaşa yazılmaktadır:

```
      // İlgili ofsete atlama
      cfs_seek(write_fd, gelen_paket->ofset, CFS_SEEK_SET);
      // Pakette gelen veriyi yazma
      int yazilan = cfs_write(write_fd, gelen_paket->veri, gelen_paket->uzunluk);
      
      // Eğer verinin tamamı yazıldıysa ACK hazırlanır.
      if (yazilan == gelen_paket->uzunluk) {
        cevap_ack.durum = 0;
        LOG_INFO("Paket %u alindi ve yazildi.\n", gelen_paket->paket_num);
```

Burada pakette belirtilen ofsete atlanılarak pakette gelen veri flaşa yazılmaktadır. Verinin tamamı başarıyla yazıldıysa ACk durumu 0 (başarılı) yapılarak devamında istemci düğüme gönderilmektedir.


ALINAN ÖNLEMLER

* Paket Bütünlüğünün Korunması İçin Önlem
Paketler gönderilirken hem istemci hem de sunucu tarafında CRC-16 algoritmasına göre paketlerin sağlama toplam deeğrleri hesaplanarak birbirleiyle karşılaştırılmaktadır. Eğer bozulma varsa sunucu tarafında belirlenerek ACK paketiyle istemciye bildirilir ve paket tekrar gönderilmektedir. Böylece veri bloklarının doğru iletilmesi sağlanmaktadır. Kullanılan algoritma şu şekildedir:

```
static uint16_t checksum_hesapla(const uint8_t *veri, uint16_t uzunluk) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < uzunluk; i++) {
    crc ^= (uint16_t)veri[i] << 8;
    for (uint16_t j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021; // CCITT Polinomu
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}
```

Bu algoritma aldığı veriyi soldan sağa byte byte ilerleyerek incelemekte ve CCITT polinomuna bölme ilkesine dayanmaktadır. Bu işlemler sonucunda veriyi temsil eden 16 bitlik bir sayı elde edilmektedir.

* Dosya Bütünlüğünün Korunması İçin Önlem
Tüm paketlerin gönderimi tamamlandıktan sonra sunucu CRC-32 algoritmasıyla dosya için bir değer hesaplamakta ve bu değeri istemci tarafından hesaplanan değerle karşılaştırarak paketlerin düzgün geldiğini ve flaşa doğru bir şekilde yazıldığını doğrulamaktadır. Kullanılan algoritma şu şekildedir:

```
uint32_t hesapla_crc32(const uint8_t *data, size_t len, uint32_t previous_crc) {
  uint32_t crc = ~previous_crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}
```

Bu algoritmada ise veri sağdan sola doğru parça parça işlenerek 32 bitlik daha büyük bir sayıyla işleme alınmaktadır. Burada veri parça parça ve daha önce hesaplanan değerle ("previus_crc") bir döngü içerisinde algoritmaya verilmektedir. Bu işlemler sonucunda veriyi temsil eden 32 bitlik bir sayı elde edilmektedir.
