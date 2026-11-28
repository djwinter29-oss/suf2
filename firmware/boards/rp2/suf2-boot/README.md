# SUF2 bootloader

This folder contains the RP2040/RP2350 boot flow, hardware flash adapter,
TinyUSB callbacks and USB descriptors. The reusable verifier, installer and
virtual drive remain in [firmware/suf2](../../../suf2).

- `port/`: the example Mbed TLS backend.
- `app/`: the RP2 application's reboot-to-update helper.
- `tests/`: host simulation, including interrupted-install recovery.

Configure through the [RP2 project](../README.md), then build the loader:

```sh
cmake --build build/rp2040 --target suf2_loader
```

Artifacts remain at `build/<board>/suf2_loader.*`.
See the [firmware guide](../../../README.md) for keys, flash layout and
security limits. This example does not configure hardware secure boot or OTP.
