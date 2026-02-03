// src/app_presenter.cpp
// Module implementation.
#include "ui/app_presenter.h"

#include "config/config.h" // appConfig()
String buildTicker(const MiningSummary &s) {
  String t;
  if (s.workHashHex_[0] != '\0') {
    t += s.workHashHex_;
    if (s.workSeed_[0] != '\0') {
      t += "|";
      t += s.workSeed_;
      t += "|";
      t += String(s.workNonce_);
    }
    return t;
  }
  t = s.logLine40_;
  t.replace('\n', ' ');
  t.replace('\r', ' ');
  t.trim();
  return t;
}
void buildPanelData(const MiningSummary &summary, UIMining &ui,
                    UIMining::PanelData &data, NetworkStatus netStatus) {
  const auto &cfg = appConfig();
  data.hrKh_ = summary.totalKh_;
  data.accepted_ = summary.accepted_;
  data.rejected_ = summary.rejected_;
  data.rejPct_ = (summary.accepted_ + summary.rejected_)
                     ? (100.0f * summary.rejected_ /
                        (float)(summary.accepted_ + summary.rejected_))
                     : 0.0f;
  data.bestShare_ = -1.0f;
  data.poolAlive_ = summary.anyConnected_;
  data.diff_ = (float)summary.maxDifficulty_;
  data.pingMs_ = summary.maxPingMs_;
  data.miningEnabled_ = summary.miningEnabled_;
  data.elapsedS_ = ui.uptimeSeconds();
  data.sw_ = cfg.appVersion_;
  data.fw_ = ui.shortFwString();
  data.poolName_ = summary.poolName_;
  data.worker_ = cfg.ducoRigName_;
  {
    switch (netStatus) {
    case NetworkStatus::Connected:
      data.wifiDiag_ = "WiFi connection is OK";
      break;
    case NetworkStatus::NoSsid:
      data.wifiDiag_ = "SSID not found. Check the AP name and power.";
      break;
    case NetworkStatus::ConnectFailed:
      data.wifiDiag_ = "Check the WiFi password and encryption settings.";
      break;
    default:
      data.wifiDiag_ = "Check your router and signal strength.";
      break;
    }
  }
  data.poolDiag_ = summary.poolDiag_;
}
