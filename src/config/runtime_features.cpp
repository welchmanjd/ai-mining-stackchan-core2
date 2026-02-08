// Module implementation.
#include "config/runtime_features.h"

#include "config/mc_config_store.h"
RuntimeFeatures getRuntimeFeatures() {
  RuntimeFeatures f;
  const char* wifiSsid = mcCfgWifiSsid();
  const char* ducoUser = mcCfgDucoUser();
  const char* openAiKey = mcCfgOpenAiKey();
  const char* azRegion = mcCfgAzRegion();
  const char* azKey    = mcCfgAzKey();
  const char* azVoice  = mcCfgAzVoice();
  const bool wifiEnabled = mcCfgWifiEnabled();
  const bool miningEnabled = mcCfgMiningEnabled();
  const bool aiEnabled = mcCfgAiEnabled();
  f.wifiConfigured_ = wifiSsid && *wifiSsid;
  f.wifiEnabled_    = wifiEnabled;
  f.miningEnabled_  = wifiEnabled &&
                     miningEnabled &&
                     (ducoUser && *ducoUser);
  f.aiEnabled_      = wifiEnabled &&
                     aiEnabled &&
                     (openAiKey && *openAiKey);
  f.ttsEnabled_     = wifiEnabled &&
                     (azRegion && *azRegion) &&
                     (azKey    && *azKey) &&
                     (azVoice  && *azVoice);
  return f;
}
