# Changelog

## 1.0.0 (2023-09-19)


### Features

* 🎸 Additional features for provisioning and init bootstrap ([d183a53](https://github.com/onomondo/nrf-softsim-dev/commit/d183a53075945eb7623dffd3bc7b9fb4714dd64a))
* 🎸 Switch to NVS, use TFM for keys, use cached data ([663cceb](https://github.com/onomondo/nrf-softsim-dev/commit/663cceb3ad541f36f5b04d2079b1207a5bd461b5))


### Bug Fixes

* 🐛 Add return code to provision ([d183a53](https://github.com/onomondo/nrf-softsim-dev/commit/d183a53075945eb7623dffd3bc7b9fb4714dd64a))
* 🐛 Add semaphore to avoid collisions during provision ([e5e0a1d](https://github.com/onomondo/nrf-softsim-dev/commit/e5e0a1dd6f2afeae9ced33be82395fdfe83210be))
* 🐛 Fixed issue where SoftSIM re-init would fail ([b92ab73](https://github.com/onomondo/nrf-softsim-dev/commit/b92ab73c33723ce2f1ab873f2b099f96d5273122))


### Performance Improvements

* ⚡️ Only protect PSA_xxx with sem ([d091517](https://github.com/onomondo/nrf-softsim-dev/commit/d091517dee35b287f175e1184fcc718db0f681b1))
