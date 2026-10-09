#include "LisekOta.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "lisek_pubkey.h"

// Keep the bootloader's rollback armed: the Arduino core would otherwise mark
// every freshly installed image valid as soon as it starts.
extern "C" bool verifyRollbackLater() {
  return true;
}

namespace LisekOta {

static const char *SVC_UUID = "4c49534b-0001-4f54-8000-00805f9b34fb";
static const char *INFO_UUID = "4c49534b-0002-4f54-8000-00805f9b34fb";  // read: JSON
static const char *CTRL_UUID = "4c49534b-0003-4f54-8000-00805f9b34fb";  // write commands, notify results
static const char *DATA_UUID = "4c49534b-0004-4f54-8000-00805f9b34fb";  // write: header + image

static const uint32_t CONFIRM_TIMEOUT_MS = 5 * 60 * 1000;

// Signed image header, made by software/ota/sign.py. The signature covers
// SHA-256 of bytes [0, SIGNED_LEN), which include the image's own SHA-256.
static const size_t HDR_LEN = 168, SIGNED_LEN = 92;
struct __attribute__((packed)) Header {
  char magic[4];  // "LSK1"
  char role[12];
  char version[40];
  uint32_t size;
  uint8_t sha256[32];
  uint8_t sig_len;
  uint8_t sig[72];  // DER ECDSA P-256
  uint8_t pad[3];
};
static_assert(sizeof(Header) == HDR_LEN, "header layout");

static const char *g_role = "";
static bool g_pending = false, g_healthy = false;
static uint16_t g_mtu = 23;
static volatile uint8_t g_id = 0;
static char g_name[16];

enum State { IDLE, HEADER, IMAGE, DONE, FAILED };
static volatile State g_state = IDLE;
static uint8_t g_hdr_buf[HDR_LEN];
static size_t g_hdr_len = 0;
static volatile uint32_t g_written = 0, g_total = 0;
static uint32_t g_last_note = 0;
static mbedtls_sha256_context g_sha;

static BLECharacteristic *g_info, *g_ctrl;
static void refresh_adv();
static esp_timer_handle_t g_reboot_timer, g_deadline_timer;

static void notify(const String &s) {
  g_ctrl->setValue(s);
  g_ctrl->notify();
}

static void fail(const String &why) {
  if (g_state == IMAGE) Update.abort();
  g_state = FAILED;
  notify("error:" + why);
  refresh_adv();
}

static bool header_ok(const Header &h, String &why) {
  if (memcmp(h.magic, "LSK1", 4) != 0) return why = "not a signed image", false;
  if (strncmp(h.role, g_role, sizeof h.role) != 0) return why = "image is for another device type", false;
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  if (!next || h.size == 0 || h.size > next->size) return why = "image does not fit", false;
  if (h.sig_len == 0 || h.sig_len > sizeof h.sig) return why = "bad signature", false;

  uint8_t digest[32];
  mbedtls_sha256((const uint8_t *)&h, SIGNED_LEN, digest, 0);
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int r = mbedtls_pk_parse_public_key(&pk, LISEK_PUBKEY_DER, sizeof LISEK_PUBKEY_DER);
  if (r == 0) r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof digest, h.sig, h.sig_len);
  mbedtls_pk_free(&pk);
  if (r != 0) return why = "bad signature", false;
  return true;
}

static void on_data(const uint8_t *p, size_t n) {
  if (g_state == HEADER) {
    size_t take = min(n, HDR_LEN - g_hdr_len);
    memcpy(g_hdr_buf + g_hdr_len, p, take);
    g_hdr_len += take;
    p += take;
    n -= take;
    if (g_hdr_len < HDR_LEN) return;
    const Header &h = *(const Header *)g_hdr_buf;
    String why;
    if (!header_ok(h, why)) return fail(why);
    if (!Update.begin(h.size)) return fail(String("begin: ") + Update.errorString());
    mbedtls_sha256_init(&g_sha);
    mbedtls_sha256_starts(&g_sha, 0);
    g_total = h.size;
    g_written = 0;
    g_last_note = 0;
    g_state = IMAGE;
    notify("header-ok");
    refresh_adv();
  }
  if (g_state != IMAGE || n == 0) return;
  if (g_written + n > g_total) return fail("too much data");
  if (Update.write((uint8_t *)p, n) != n) return fail(String("write: ") + Update.errorString());
  mbedtls_sha256_update(&g_sha, p, n);
  g_written += n;
  if (g_written - g_last_note >= 32768 || g_written == g_total) {
    g_last_note = g_written;
    notify("progress:" + String(g_written) + "/" + String(g_total));
  }
  if (g_written < g_total) return;

  uint8_t digest[32];
  mbedtls_sha256_finish(&g_sha, digest);
  mbedtls_sha256_free(&g_sha);
  if (memcmp(digest, ((const Header *)g_hdr_buf)->sha256, 32) != 0) return fail("checksum mismatch");
  // esp_ota_end inside checks the image itself too, then makes it the boot image.
  if (!Update.end()) {
    g_state = FAILED;
    return notify(String("error:end: ") + Update.errorString());
  }
  g_state = DONE;
  notify("done");
  refresh_adv();
  esp_timer_start_once(g_reboot_timer, 800 * 1000);  // boot the new image, which must then be confirmed
}

static void on_command(const String &cmd) {
  if (cmd == "begin") {
    if (g_state == IMAGE) Update.abort();
    g_hdr_len = 0;
    g_state = HEADER;
    notify("ready");
  } else if (cmd == "abort") {
    if (g_state == IMAGE) Update.abort();
    g_state = IDLE;
    notify("aborted");
  } else if (cmd == "confirm") {
    if (!g_pending) return notify("confirmed");
    if (!g_healthy) return notify("error:not healthy yet");
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return notify("error:confirm failed");
    g_pending = false;
    esp_timer_stop(g_deadline_timer);
    notify("confirmed");
    refresh_adv();
  } else if (cmd == "reboot") {
    notify("rebooting");
    esp_timer_start_once(g_reboot_timer, 300 * 1000);
  } else {
    notify("error:unknown command");
  }
}

static const char *state_name() {
  return g_state == IMAGE ? "updating" : g_state == DONE ? "installed" : g_pending ? "pending" : "ok";
}

// Scan response: name, then manufacturer data (company 0xFFFF, for testing and
// internal use) = role letter, state letter, id digit, h(ealthy)/u, version. A web page
// scanning for advertisements sees all of it without connecting.
static void refresh_adv() {
  if (!g_info) return;
  String m = "\xff\xff";
  m += (char)toupper(g_role[0]);
  m += state_name()[0];
  m += (char)('0' + g_id % 10);  // printable: String stops at a 0 byte
  m += g_healthy ? 'h' : 'u';
  m += String(LISEK_VERSION).substring(0, 31 - 2 - strlen(g_name) - 2 - 6);
  BLEAdvertisementData sr;
  sr.setName(g_name);
  sr.setManufacturerData(m);
  BLEDevice::getAdvertising()->setScanResponseData(sr);
}

static String info_json() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  const char *state = state_name();
  String j = "{\"role\":\"";
  j += g_role;
  j += "\",\"version\":\"" LISEK_VERSION "\",\"state\":\"";
  j += state;
  j += "\",\"healthy\":";
  j += g_healthy ? "true" : "false";
  j += ",\"partition\":\"";
  j += run ? run->label : "?";
  j += "\",\"mtu\":";
  j += g_mtu;
  j += ",\"id\":";
  j += g_id;
  j += "}";
  return j;
}

class InfoCb : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *c) override {
    c->setValue(info_json());
  }
};
class CtrlCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    on_command(String((const char *)c->getData(), c->getLength()));
  }
};
class DataCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    on_data(c->getData(), c->getLength());
  }
};
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    g_mtu = 23;
  }
  void onDisconnect(BLEServer *) override {
    if (g_state == HEADER || g_state == IMAGE) {
      if (g_state == IMAGE) Update.abort();
      g_state = IDLE;
    }
    BLEDevice::startAdvertising();
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    g_mtu = param->mtu.mtu;
  }
};

void begin(const char *role) {
  g_role = role;

  esp_timer_create_args_t reboot_args = {};
  reboot_args.callback = [](void *) { esp_restart(); };
  reboot_args.name = "lisek-reboot";
  esp_timer_create(&reboot_args, &g_reboot_timer);

  // A freshly installed image: roll back unless it is confirmed in time.
  // Runs from the timer task, so a hung sketch cannot stop it.
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    g_pending = true;
    esp_timer_create_args_t deadline_args = {};
    deadline_args.callback = [](void *) { esp_ota_mark_app_invalid_rollback_and_reboot(); };
    deadline_args.name = "lisek-rollback";
    esp_timer_create(&deadline_args, &g_deadline_timer);
    esp_timer_start_once(g_deadline_timer, (uint64_t)CONFIRM_TIMEOUT_MS * 1000);
  }

  uint64_t mac = ESP.getEfuseMac();
  snprintf(g_name, sizeof g_name, "lisek-%c-%02X%02X", toupper(role[0]), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  BLEDevice::init(g_name);
  BLEDevice::setMTU(517);
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new ServerCb());
  BLEService *svc = srv->createService(SVC_UUID);
  g_info = svc->createCharacteristic(INFO_UUID, BLECharacteristic::PROPERTY_READ);
  g_info->setCallbacks(new InfoCb());
  g_ctrl = svc->createCharacteristic(CTRL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  g_ctrl->addDescriptor(new BLE2902());
  g_ctrl->setCallbacks(new CtrlCb());
  BLECharacteristic *data = svc->createCharacteristic(DATA_UUID, BLECharacteristic::PROPERTY_WRITE);
  data->setCallbacks(new DataCb());
  svc->start();

  // Slow advertising (about once a second) keeps the radio's share of the
  // battery small; the name goes in the scan response to leave room for the
  // 128-bit service id.
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  BLEAdvertisementData ad;
  ad.setFlags(0x06);
  ad.setCompleteServices(BLEUUID(SVC_UUID));
  adv->setAdvertisementData(ad);
  refresh_adv();
  adv->setMinInterval(1600);  // 1000 ms
  adv->setMaxInterval(1760);  // 1100 ms
  BLEDevice::startAdvertising();
}

void markHealthy() {
  g_healthy = true;
  refresh_adv();
}

void setId(uint8_t id) {
  g_id = id;
  refresh_adv();
}

bool updating() {
  return g_state == IMAGE;
}

uint32_t progress(uint32_t *total) {
  if (total) *total = g_total;
  return g_written;
}

}  // namespace LisekOta
