#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "cfs/cfs.h" // Coffee FS kütüphanesi

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_DBG

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

/* Veri boyutu tanımlaması */
#define VERI_BOYUTU 64

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

/* Statik değişken tanımları */
static int write_fd = -1;

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

static struct simple_udp_connection udp_conn;

PROCESS(udp_server_process, "UDP server");
AUTOSTART_PROCESSES(&udp_server_process);
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
  struct firmware_paket *gelen_paket = (struct firmware_paket *)data;
  struct ack_paket cevap_ack;
  cevap_ack.durum = 2;

  /* CRC-16 sağlama toplamı kontrolü */
  uint16_t hesaplanan_chksm = checksum_hesapla(gelen_paket->veri, gelen_paket->uzunluk);

  // Sağlama toplamı eşleşiyorsa gelen paket işleme alınır.
  if (gelen_paket->checksum == hesaplanan_chksm) {

    /* Gelen ilk paketse yazma için "new-firmware.z1" dosyası açılır. */
    if (gelen_paket->paket_tipi == 1) {
      write_fd = cfs_open("new-firmware.z1", CFS_READ | CFS_WRITE);
    }

    /* Gelen paketi flaşa yazma işlemleri */
    if (write_fd >= 0) {

      // İlgili ofsete atlama
      cfs_seek(write_fd, gelen_paket->ofset, CFS_SEEK_SET);
      // Pakette gelen veriyi yazma
      int yazilan = cfs_write(write_fd, gelen_paket->veri, gelen_paket->uzunluk);
      
      // Eğer verinin tamamı yazıldıysa ACK hazırlanır.
      if (yazilan == gelen_paket->uzunluk) {
        cevap_ack.durum = 0;
        LOG_INFO("Paket %u alindi ve yazildi.\n", gelen_paket->paket_num);
        
        // Eğer gelen paket son paketse CRC-32 ile dosya doğrulaması yapılır.
        if (gelen_paket->paket_tipi == 2) {

          LOG_INFO("Tum paketler alindi. Yeni firmware dogrulamasi basliyor...\n");

          /* Alınan firmware'in CRC32 ile doğrulanması */
          uint32_t hesaplanan_crc = 0;

          /* Dosya boyutunu belirleme */
          cfs_seek(write_fd, 0, CFS_SEEK_SET);
          uint32_t dosya_boyutu = (uint32_t) cfs_seek(write_fd, 0, CFS_SEEK_END);
          cfs_seek(write_fd, 0, CFS_SEEK_SET); // İmleci başa alma
	    
          /* Dosya toplam CRC32 değerini hesaplama */
          uint8_t temp_buf[64];
          int n;
          uint32_t toplam_okunan = 0;
		   
          while(toplam_okunan < dosya_boyutu) {
            uint32_t okunacak = (dosya_boyutu - toplam_okunan > 64) ? 64 : (dosya_boyutu - toplam_okunan);
            
            n = cfs_read(write_fd, temp_buf, okunacak);
            if(n <= 0) break;

            hesaplanan_crc = hesapla_crc32(temp_buf, n, hesaplanan_crc);
            toplam_okunan += n;
	        }

          LOG_INFO("Hesaplanan CRC32: %08lX\n", (unsigned long)hesaplanan_crc);
          LOG_INFO("Gelen CRC32: %08lX\n", (unsigned long)gelen_paket->ek_veri);

          // Pakette gelen sağlama toplamı ile karşılaştırma
          if(hesaplanan_crc == gelen_paket->ek_veri) {
            LOG_INFO("Yuklenmeye hazir yeni firmware alimi tamamlandi. CRC OK!\n");
          } else {
            LOG_ERR("Butunluk Hatasi! Beklenen: %08lX, Hesaplanan: %08lX\nDosya siliniyor...\n",
                    (unsigned long)gelen_paket->ek_veri, (unsigned long)hesaplanan_crc);
            cfs_remove("new-firmware.z1");
          }

          // Yazma dosya belirteci kapatılır.
          cfs_close(write_fd);
          write_fd = -1;
        }
      } else { // Gelen paketteki veri yazılamadıysa
        cevap_ack.durum = 2;
        LOG_ERR("HATA: Flasa yazma hatasi!\n");
      }

    } else { // "new-firmware.z1" dosyası açılamadıysa
      cevap_ack.durum = 2;
      LOG_ERR("HATA: Flasa yazma hatasi! new-firmware.z1 dosyasi acilamadi.\n");
    }
  } else { // Paket doğrulama hatası varsa
    cevap_ack.durum = 1;
    LOG_ERR("HATA: Checksum hatasi! Paket: %u\n", gelen_paket->paket_num);
  }

  /* ACK gönderme */
  cevap_ack.paket_num = gelen_paket->paket_num;
  simple_udp_sendto(&udp_conn, &cevap_ack, sizeof(cevap_ack), sender_addr);
}

/*---------------------------------------------------------------------------*/

PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();


/* Initialize DAG root */
  NETSTACK_ROUTING.root_start();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
