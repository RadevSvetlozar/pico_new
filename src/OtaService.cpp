#include "App.h"
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

namespace {
String hexDigest(const unsigned char* digest, size_t length) {
  static const char hex[] = "0123456789abcdef";
  String output;
  output.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    output += hex[digest[index] >> 4];
    output += hex[digest[index] & 0x0F];
  }
  return output;
}

bool secureEquals(const String& left, const String& right) {
  if (left.length() != right.length()) return false;
  uint8_t difference = 0;
  for (size_t index = 0; index < left.length(); ++index) {
    difference |= static_cast<uint8_t>(left[index] ^ right[index]);
  }
  return difference == 0;
}

String hmacSha256(const String& value, const String& key) {
  unsigned char digest[32];
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info || mbedtls_md_hmac(info,
                               reinterpret_cast<const unsigned char*>(key.c_str()),
                               key.length(),
                               reinterpret_cast<const unsigned char*>(value.c_str()),
                               value.length(), digest) != 0) return "";
  return hexDigest(digest, sizeof(digest));
}

bool installFirmware(const String& url, const String& expectedSha256) {
  appLog("OTA", "Update started, URL=" + url);
  HTTPClient http;
  http.begin(url);
  const int httpCode = http.GET();
  appLog("OTA", "HTTP response code=" + String(httpCode));
  if (httpCode != HTTP_CODE_OK) {
    mqttLog("HTTP GET failed with code: " + String(httpCode), ERROR);
    http.end();
    return false;
  }
  const int contentLength = http.getSize();
  appLog("OTA", "Firmware size=" + String(contentLength) + " bytes");
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    mqttLog("Update begin failed.", ERROR);
    http.end();
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t written = 0;
  while (http.connected() &&
         (contentLength < 0 || written < static_cast<size_t>(contentLength))) {
    const size_t available = stream->available();
    if (!available) { delay(1); continue; }
    const size_t count = stream->readBytes(buffer, min(available, sizeof(buffer)));
    if (!count) continue;
    mbedtls_sha256_update_ret(&sha, buffer, count);
    if (Update.write(buffer, count) != count) {
      mqttLog("Update write failed.", ERROR);
      Update.abort();
      mbedtls_sha256_free(&sha);
      http.end();
      return false;
    }
    written += count;
  }
  unsigned char digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);
  const String actualSha256 = hexDigest(digest, sizeof(digest));
  appLog("OTA", "Written " + String(written) + "/" +
                    String(contentLength) + " bytes, sha256=" + actualSha256);
  if (expectedSha256.length() &&
      !secureEquals(actualSha256, expectedSha256)) {
    mqttLog("Firmware SHA-256 verification failed; update aborted.", ERROR);
    Update.abort();
    http.end();
    return false;
  }
  if (!Update.end() || !Update.isFinished()) {
    mqttLog("Update failed. Error " + String(Update.getError()), ERROR);
    http.end();
    return false;
  }
  http.end();
  mqttLog("Update completed and verified. Rebooting.");
  delay(300);
  ESP.restart();
  return true;
}
}  // namespace

void performOtaUpdate(const String& url) {
  // Backward-compatible URL OTA remains unchanged for existing callers.
  installFirmware(url, "");
}

void performOtaManifestUpdate(const String& payload) {
  StaticJsonDocument<1024> manifest;
  if (deserializeJson(manifest, payload)) {
    mqttLog("Invalid OTA manifest JSON.", ERROR);
    return;
  }
  const String board = manifest["board"] | "";
  const String version = manifest["version"] | "";
  const String url = manifest["url"] | "";
  String sha256 = manifest["sha256"] | "";
  String signature = manifest["signature"] | "";
  sha256.toLowerCase();
  signature.toLowerCase();
  if (board != "arduino_nano_esp32" || !version.length() || url.length() < 6 ||
      sha256.length() != 64 || signature.length() != 64 || !adminPass.length()) {
    mqttLog("Incomplete OTA manifest or missing device admin secret.", ERROR);
    return;
  }
  const String canonical = version + "|" + url + "|" + sha256;
  if (!secureEquals(signature, hmacSha256(canonical, adminPass))) {
    mqttLog("OTA manifest signature verification failed.", ERROR);
    return;
  }
  if (version == firmwareVersion) {
    appLog("OTA", "Manifest version already installed: " + version);
    return;
  }
  installFirmware(url, sha256);
}
