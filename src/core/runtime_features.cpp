// Module implementation.
#include "core/runtime_features.h"
#include "config/mc_config_store.h"
RuntimeFeatures getRuntimeFeatures() {
  RuntimeFeatures f;
  const char* wifiSsid = mcCfgWifiSsid();
  const char* ducoUser = mcCfgDucoUser();
  const char* azRegion = mcCfgAzRegion();
  const char* azKey    = mcCfgAzKey();
  const char* azVoice  = mcCfgAzVoice();
  f.wifiConfigured_ = wifiSsid && *wifiSsid;
  f.miningEnabled_  = ducoUser && *ducoUser;
  f.ttsEnabled_     = (azRegion && *azRegion) &&
                     (azKey    && *azKey) &&
                     (azVoice  && *azVoice);
  return f;
}
