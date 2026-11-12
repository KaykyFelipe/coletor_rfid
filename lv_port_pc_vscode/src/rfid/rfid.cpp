#include "rfid.h"

#include <regex>

static std::vector<rfid_tag_t> tags;
static std::vector<rfid_tag_t> reads;
static bool reading = false;

static bool accepted(const STRING &epc) {
  static const std::regex filter(RFID_TAG_FILTER);
  return std::regex_match(epc.c_str(), filter);
}

void rfid_init() {
  rfid_backend_init();
}

void rfid_start(uint64_t now_ms) {
  tags.clear();
  reading = true;
  rfid_backend_start(now_ms);
}

void rfid_poll(uint64_t now_ms) {
  if (!reading) return;

  reads.clear();
  rfid_backend_poll(now_ms, reads);

  for (const rfid_tag_t &read : reads) {
    if (!accepted(read.epc)) continue;

    bool known = false;
    for (rfid_tag_t &tag : tags) {
      if (tag.epc == read.epc) {
        tag.reads += read.reads;
        if (read.rssi > tag.rssi) tag.rssi = read.rssi;
        known = true;
        break;
      }
    }
    if (!known) tags.push_back(read);
  }
}

void rfid_stop() {
  if (!reading) return;
  reading = false;
  rfid_backend_stop();
}

bool rfid_is_reading() {
  return reading;
}

const std::vector<rfid_tag_t> &rfid_tags() {
  return tags;
}

bool rfid_best_tag(rfid_tag_t &best) {
  if (tags.empty()) return false;

  best = tags[0];
  for (const rfid_tag_t &tag : tags) {
    // RFID_NO_RSSI e o menor valor possivel: sem RSSI, decide a quantidade de leituras.
    if (tag.rssi > best.rssi || (tag.rssi == best.rssi && tag.reads > best.reads)) {
      best = tag;
    }
  }
  return true;
}
