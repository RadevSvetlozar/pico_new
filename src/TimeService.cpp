#include "App.h"

#include <time.h>

namespace {
// POSIX TZ string for Europe/Sofia:
// UTC+2 in winter and UTC+3 from the last Sunday in March until the last
// Sunday in October.
constexpr char SOFIA_TIMEZONE[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char PRIMARY_NTP_SERVER[] = "pool.ntp.org";
constexpr char SECONDARY_NTP_SERVER[] = "time.google.com";
constexpr char FALLBACK_NTP_SERVER[] = "time.cloudflare.com";
constexpr time_t MIN_VALID_EPOCH = 1704067200;  // 2024-01-01 00:00:00 UTC
constexpr unsigned long RETRY_INTERVAL_MS = 60000;

bool ntpStarted = false;
bool synchronizationReported = false;
unsigned long lastAttemptAt = 0;

bool clockIsValid() {
  return time(nullptr) >= MIN_VALID_EPOCH;
}

void startNtp() {
  configTzTime(SOFIA_TIMEZONE, PRIMARY_NTP_SERVER, SECONDARY_NTP_SERVER,
               FALLBACK_NTP_SERVER);
  ntpStarted = true;
  lastAttemptAt = millis();
  appLog("TIME", "NTP synchronization started");
}
}  // namespace

bool isTimeSynchronized() { return clockIsValid(); }

String currentLogTimestamp() {
  if (!clockIsValid()) return String(millis()) + "ms";

  const time_t now = time(nullptr);
  struct tm localTime;
  if (localtime_r(&now, &localTime) == nullptr) {
    return String(millis()) + "ms";
  }

  char timestamp[32];
  if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S %Z",
               &localTime) == 0) {
    return String(millis()) + "ms";
  }
  return String(timestamp);
}

void processTimeSynchronization() {
  if (clockIsValid()) {
    if (!synchronizationReported) {
      synchronizationReported = true;
      appLog("TIME", "NTP synchronized; local timezone=Europe/Sofia");
    }
    return;
  }

  if (!mqttClient.isNetworkConnected()) return;
  if (!ntpStarted || millis() - lastAttemptAt >= RETRY_INTERVAL_MS) startNtp();
}
