#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "cfs/cfs.h" // Coffee FS kütüphanesi
#include "new-firmware.h" // Yeni firmware verisi

#include "sys/node-id.h"
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_DBG //LOG_LEVEL_INFO

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

/*---------------------------------------------------------------------------------*/
/* Veri boyutu tanımlaması */
#define VERI_BOYUTU 64
/* ACK bekleme süresi tanımlaması */
#define TIMEOUT (10 * CLOCK_SECOND)

/* Yeni firmware veri dizisi tanımı */
#define VERI_DIZISI new_firmware_z1
#define VERI_DIZISI_BOYUTU new_firmware_z1_len

/* Paket veri yapısı tanımlaması */
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

/* Paketin alındığına dair ACK veri yapısı tanımı */
struct __attribute__((packed)) ack_paket {
    uint16_t paket_num;
    uint8_t durum; // 0: başarılı 1: bozuk (tekrar gönder) 2: yazma hatası
};

/* --- OTA Süreç Kontrol Değişkenleri --- */
static uint16_t suanki_paket_sirasi = 0; // Şu an gönderilmekte olan paket numarası
static bool ack_geldi_mi = false; // ACK gelene kadar yeni paket gönderimini durdurur
static bool aktarim_bitti_mi = false; // Dosya aktarımı tamamen bitti mi?
static uint32_t dosya_boyutu = 0; // 129760
static uint16_t toplam_paket_sayisi = 0; //2028
static int fd = -1; // Dosya belirteci

/* Dosya boyutu ve paketteki veri boyutuna göre gönderilecek toplam paket sayısını hesaplar.*/
static uint16_t paket_sayisi_hesapla() {
  return toplam_paket_sayisi = (dosya_boyutu % VERI_BOYUTU == 0 ? dosya_boyutu/VERI_BOYUTU : dosya_boyutu/VERI_BOYUTU + 1);
}

/* CRC-16/CCITT algoritması ile gönderilen her bir paketin sağlama toplamı hesaplanır. Paket doğruluğu için kullanılır. */
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

/* CRC-32 algoritmasıyla tam dosyanın sağlama toplamı hesaplanır. Firmware dosyasnın doğruluğu için kullanılır. */
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

/* Gönderilecek yeni firmware dosyasını düğüm dosya sistemine yazma fonksiyonu */
/* Bu fonksiyon "cooja mote" tipine uygun olarak "new-firmware.h" dosyasının içindeki yeni firmware yazılımını
  "new-firmware.bin" gönderici düğümün Coffee dosya sistemine tek seferde kaydeder. */
static uint8_t yeni_firmware_kaydet(void)
{
  int fd = cfs_open("new-firmware.z1", CFS_WRITE);
  
  if(fd < 0) {
    LOG_ERR("new-firmware.z1 olusturulamadi!\n");
    return -1;
  }

  LOG_INFO("Yaziliyor: %lu byte veri aktariliyor...\n", (unsigned long)VERI_DIZISI_BOYUTU);
  
  int yazilan = cfs_write(fd, VERI_DIZISI, VERI_DIZISI_BOYUTU);
  cfs_close(fd);

  if(yazilan != (int)VERI_DIZISI_BOYUTU) {
    LOG_ERR("Yazilmasi gereken %lu byte iken %d byte yazilabildi!\n", 
            (unsigned long)VERI_DIZISI_BOYUTU, yazilan);
    return -2;
  }

  return 0;
}

/*---------------------------------------------------------------------------------*/

static struct simple_udp_connection udp_conn;

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/

static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  /* ID:3 düğümün proje kapsamında işlevsiz olduğu için sadece ID:2 düğümü gelen paketi işler.*/
  if (node_id ==2) {

    struct ack_paket *gelen_ack = (struct ack_paket *)data;

    // Gelen ACK paketinin bütünlüğü ile ilgili ufak doğrulamalar yapılır.
    if (datalen == sizeof(struct ack_paket) && gelen_ack->paket_num == suanki_paket_sirasi) {

      if (gelen_ack->durum == 0) { // İletim başarılı
        ack_geldi_mi = true;
        process_post(&udp_client_process, PROCESS_EVENT_CONTINUE, NULL);
      } else if (gelen_ack->durum == 1) { // CRC-16 doğrulama hatası
        LOG_ERR("Paket bozulmus! Tekrar gonderilecek.\n");
      } else { // Flaşa yazma hatası ve diğer hatalar
        LOG_ERR("Yazma hatasi! Tekrar gonderilecek.\n");
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer timeout_timer;
  static struct etimer periodic_timer;
  uip_ipaddr_t dest_ipaddr;
  static uint32_t dosya_toplam_crc = 0;
  static struct firmware_paket pkt;

  PROCESS_BEGIN();
         
  // ID:3 düğümünün boş yere kaynak tüketmemesi için sadece ID:2 düğümü işlem yapar.
  if (node_id == 2) {
  
    // Yeni firmware verisi düğüm dosya sistemine yazılır.
    yeni_firmware_kaydet();
  
    // Yeni firmware dosyası Coffee dosya sisteminde okuma modunda açılır ve açıldığı kontrol edilir.
    fd = cfs_open("new-firmware.z1", CFS_READ);
    if(fd < 0) {
      LOG_ERR("HATA: new-firmware.z1 acilamadi!\n");
    } else {
      LOG_INFO("Dosya okuma icin hazir.\n");

      // Dosya boyutu "CFS_SEEK_END" parametresi ile belirlenerek global değişkene atanır.
      dosya_boyutu = (uint32_t) cfs_seek(fd, 0, CFS_SEEK_END);
      cfs_seek(fd, 0, CFS_SEEK_SET); // İmleci başa alma
      LOG_INFO("Dosya boyutu: %d\n", dosya_boyutu);
      
      // Dosya sistemindeki dosyanın CRC-32 ile sağlama toplamı alınır.
      uint8_t temp_buf[64];
      int n;
      uint32_t toplam_okunan = 0;
            
      while(toplam_okunan < dosya_boyutu) {
        uint32_t okunacak = (dosya_boyutu - toplam_okunan > 64) ? 64 : (dosya_boyutu - toplam_okunan);
      
        n = cfs_read(fd, temp_buf, okunacak);
        if(n <= 0) break;

        dosya_toplam_crc = hesapla_crc32(temp_buf, n, dosya_toplam_crc);
        toplam_okunan += n;
      }
      LOG_INFO("Dosya CRC32: %08lX\n", (unsigned long)dosya_toplam_crc);
      cfs_seek(fd, 0, CFS_SEEK_SET); // İmleci başa alma
    }
  
    // Toplam paket sayısı hesaplanarak "pkt" yapısına eklenir.  
    pkt.toplam_paket_sayisi = paket_sayisi_hesapla();

    /* UDP bağlantısının başlatılması */
    simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
    UDP_SERVER_PORT, udp_rx_callback);
  }
  
  // Aktarım bitene kadar ID:2 düğümü paketlari hazırlayıp sırasıyla gönderir.
  while(!aktarim_bitti_mi && node_id == 2 && fd >= 0) {


    if(NETSTACK_ROUTING.node_is_reachable() &&
        NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

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

        /* ACK durumunu false yapma */
        ack_geldi_mi = false;

        /* Paketi gönderme ve zamanlayıcı başlatma */
        simple_udp_sendto(&udp_conn, &pkt, sizeof(pkt), &dest_ipaddr);
        etimer_set(&timeout_timer, TIMEOUT);

        /* Bekleme (Ya ACK gelecek ya da zaman aşımı olacak) */
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE || etimer_expired(&timeout_timer));

        // ACK geldiyse paket sırası artırılır.
        if (ack_geldi_mi) {
          LOG_INFO("Paket %u onaylandi.\n", suanki_paket_sirasi);
          suanki_paket_sirasi++;

          //Son pakete gelindiyse aktarımı tamamlama ve dosya belirtecini kapatma
          if (suanki_paket_sirasi >= toplam_paket_sayisi) {
            aktarim_bitti_mi = true;
            cfs_close(fd);
            LOG_INFO("Aktarim tamamlandi!\n");
          }
        } else {
          LOG_WARN("Zaman asimi! Paket %u tekrar gonderiliyor...\n", suanki_paket_sirasi);
          // While döngüsünün başına dönülür.
      	}
      } else if (okunan == 0) { // Dosya sonuna gelindiyse aktarımı durdurma
        aktarim_bitti_mi = true;
        cfs_close(fd);
        LOG_INFO("Dosya sonu! Aktarim tamamlandi!\n");
      } else { // Hata durumunda aktarımı durdurma
        aktarim_bitti_mi = true;
        cfs_close(fd);
        LOG_ERR("HATA: Dosya Okuma Hatasi!\n");
	    }

    } else {
      LOG_INFO("Henuz erisilebilir degil.\n");
      etimer_set(&periodic_timer, CLOCK_SECOND);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
