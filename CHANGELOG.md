# Changelog

## [2.2.1](https://github.com/onomondo/nrf-softsim/compare/v2.2.0...v2.2.1) (2023-12-14)


### Bug Fixes

* 🐛 Heap corrupted by de-initializing softsim many times ([00bcdab](https://github.com/onomondo/nrf-softsim/commit/00bcdab2ee12965ca9cd690b2bb77c76bfca034c))

## [2.2.0](https://github.com/onomondo/nrf-softsim/compare/v2.1.0...v2.2.0) (2023-11-14)


### Features

* add missed Zephyr compatible asserts ([09be236](https://github.com/onomondo/nrf-softsim/commit/09be2366a1a26e4dfbaabc636b66aa5460dfeb7a))


### Bug Fixes

* address compilation warning for `main` return ([1a348e8](https://github.com/onomondo/nrf-softsim/commit/1a348e8d52b8611e8cd24d9a07b9bee10637148a))
* address provision check inconsistencies ([9a28958](https://github.com/onomondo/nrf-softsim/commit/9a289585e8c6188295bdab5ab36921517555102b))
* samples: change TCP to UDP ([49c9974](https://github.com/onomondo/nrf-softsim/commit/49c9974c82ed6a6f0435c12cf045d77da67c33f1))

## [2.1.0](https://github.com/onomondo/nrf-softsim/compare/v2.0.0...v2.1.0) (2023-11-06)


### Features

* make asserts Zephyr compatible ([#12](https://github.com/onomondo/nrf-softsim/issues/12)) ([bfc607b](https://github.com/onomondo/nrf-softsim/commit/bfc607b404112174002397b504b4541c970922e6))

## [2.0.0](https://github.com/onomondo/nrf-softsim/compare/v1.0.0...v2.0.0) (2023-10-10)


### ⚠ BREAKING CHANGES

* 🧨 nrf-sdk main branch is used. When v2.5 is released a tag/release for nrf-softsim is created. For now this pin to latest dev. can break your current setup.

### Features

* 🎸 Follow nrf-sdk main branch as default ([711615c](https://github.com/onomondo/nrf-softsim/commit/711615c7a248352f79a04dcb9c906d175182a26c))

## 1.0.0 (2023-09-29)


### ⚠ BREAKING CHANGES

* 🧨 ALL PREVIOS DEPLOYMENTS WILL NOT BE COMPATIBLE WITH THE LIBSTORAGE.a AND NEW PROFILE!
* NCS 2.4 integration ([#1](https://github.com/onomondo/nrf-softsim/issues/1))
* Use KMU for authentication and key management.

### Features

* 🎸 Switch profile to compact storage ([b382ecb](https://github.com/onomondo/nrf-softsim/commit/b382ecb72f9c10bb433960b9c54779a6d0030560))
* 🎸 Wow a meaningful commit ([#9](https://github.com/onomondo/nrf-softsim/issues/9)) ([e03cb18](https://github.com/onomondo/nrf-softsim/commit/e03cb18a9dd7eb072309729857851411a94bcfa5))
* Add support for dynamic template ([#4](https://github.com/onomondo/nrf-softsim/issues/4)) ([ddfac8a](https://github.com/onomondo/nrf-softsim/commit/ddfac8a3155a0dfb02a192985712110afde42afa))
* add support for static profile ([#3](https://github.com/onomondo/nrf-softsim/issues/3)) ([aca75ad](https://github.com/onomondo/nrf-softsim/commit/aca75ad8865e805269857bf4fda6db086948e02f))
* Additional features for provisioning and init bootstrap ([8eb3289](https://github.com/onomondo/nrf-softsim/commit/8eb3289b8b105dc50a57e47e70d5ed7dd1100188))
* NCS 2.4 integration ([#1](https://github.com/onomondo/nrf-softsim/issues/1)) ([dfb7864](https://github.com/onomondo/nrf-softsim/commit/dfb78649acbbc4269ec7327c88a662768aca7dca))
* Use KMU for authentication and key management. ([ffb197a](https://github.com/onomondo/nrf-softsim/commit/ffb197a6a8ca17df65dfd6bf3c292f50d2bf4f89))


### Bug Fixes

* 🐛 Add return code to provision ([8eb3289](https://github.com/onomondo/nrf-softsim/commit/8eb3289b8b105dc50a57e47e70d5ed7dd1100188))
* 🐛 Fixed issue where SoftSIM re-init would fail ([5df72b4](https://github.com/onomondo/nrf-softsim/commit/5df72b4106821eb63f516f87cbbb616a2cb3ac57))
* Bug/fix padding in cmac ota ([#5](https://github.com/onomondo/nrf-softsim/issues/5)) ([830259d](https://github.com/onomondo/nrf-softsim/commit/830259d2a5e3ed7d830a2da8f12404eca261fd2e))


### Performance Improvements

* ⚡️ Recompile w. -Os over -O2/3 ([5319011](https://github.com/onomondo/nrf-softsim/commit/5319011de8e641b68f16b9f52e2be9d9bd657b31))
