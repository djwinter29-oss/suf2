# Working on SUF2

SUF2 is a draft authenticated UF2 profile with prototype host packers and
firmware. Do not describe it as production-ready, security-reviewed, or
hardware-validated without evidence.

## Start with the relevant design

- [Specification](../docs/spec/suf2-specification.md): wire format,
  cryptographic construction, and receiver requirements.
- [Conformance catalogue](../docs/spec/conformance.md): intended test cases;
  it is not proof of implemented conformance.
- [Security design](../docs/security/security-design.md): SUF2-specific
  protections, limits, and platform responsibilities.
- [Design guidelines](../docs/implementers/design-guidelines.md) and
  [platform integration](../docs/implementers/platform-integration.md):
  receiver and board integration requirements.
- [Documentation index](../docs/README.md): reading order and document ownership.

Keep reusable material in `docs/reference/` platform- and format-independent.
Put SUF2-specific requirements in `docs/spec/` or `docs/security/`, integration
guidance in `docs/implementers/`, and release instructions in `docs/deployment/`.
Write clear, direct prose; distinguish requirements, design intent, and verified
behavior. Update related documents when behavior changes.

## Repository layout

- `host/python/`: Python CLI.
- `host/dotnet/`: .NET CLI; no UI.
- `host/dashboard/`: static browser dashboard.
- `host/shared/`: cross-producer contract tests.
- `test-keys/`: public signing examples, local test-key generator, and ignored
  signing/AES secrets in `local/`.
- `firmware/suf2/`: portable C verifier, staged installer/journal, virtual FAT16
  drive, and tests. `include/suf2/` defines their APIs, storage callbacks, and
  crypto-port contract. Keep the
  core heap-free and independent of board/platform code and crypto libraries.
- `firmware/boards/rp2/`: one CMake project for both chips, with two application
  folders: `suf2-boot/` (boot flow, hardware adapters, app helper, port and
  simulation tests) and `blink/` (example app). Keep reusable logic in
  `firmware/suf2/`. Select `SUF2_BOARD=rp2040`
  or `rp2350`; keep separate `build/rp2040` and `build/rp2350` caches.
- `firmware/boards/rp2/suf2-boot/port/`: example Mbed TLS backend, configuration, and
  build wiring. Implementations belong to board integrations, not the public
  headers or portable core. Native tests explicitly reuse this example port.
- `firmware/boards/esp32s3/`: two ESP-IDF projects, `suf2-boot/` (custom
  second-stage boot selection plus USB recovery app and ports) and `blink/`.
  Keep separate `build/esp32s3/{suf2-boot,blink}/` caches. The recovery app reuses
  the RP2 Mbed TLS source; the bootloader uses IDF ROM SHA/micro-ecc.
- `firmware/lib/`: pinned third-party git submodules. Do not edit vendored
  sources or change their revisions as a side effect of another task.
- `tools/host/`: `run_python.sh`, `run_dotnet.sh`, `run_dashboard.sh`, and
  `run_tests.sh`.
- `tools/firmware/`: `build.sh`, `flash.sh`, and key-header generation.
- `tools/release/` and `tools/validation/`: release and validation helpers.

Keep project shell scripts under `tools/`, not directly in `host/` or
`firmware/`. Preserve executable permissions. Resolve repository paths relative
to the script location so scripts work from any working directory. Build
outputs belong in ignored `build/`.

## Security and compatibility

- Authentication is mandatory: do not introduce unsigned packaging, install,
  or boot bypasses. Use established cryptographic libraries, not custom crypto.
- The draft specifies ECDSA P-256/SHA-256 and optional AES-256-GCM. Current
  packers support signing only; the verifier has no decryption backend.
  Do not claim encrypted conformance or substitute a per-process/local counter
  for the required shared AES-key lifetime invocation budget.
- Preserve low-S signatures, signed-manifest retention, and verification
  before install, after install, and on every boot.
- The three packers must agree on image conversion and signed fields.
  Randomized signatures mean packages need not be byte-identical.
- Preserve the explicit address/range/fill contract. Reject overlaps and
  invalid input; never silently rebase, fill gaps, or discard loaded bytes.
  HEX/ELF retain physical addresses; ELF BSS is not image data.
- The ESP32-S3 demo is an experimental native-container profile: sign the
  complete ESP-IDF application BIN with execution-window base `0x42000000`,
  not a physical partition offset, ELF conversion or merged flash image.
  Do not claim flat-image conformance. Retain its signed-length native-image
  check and single-authorized-partition IDF fallback list.
- Keep firmware and private-key processing local in the dashboard. Do not add
  remote scripts, analytics, key persistence, or server uploads.
- Generate development keys into ignored `test-keys/local/`. Never commit
  private signing keys, AES keys, or generated secret headers, or ship
  test-key-trusting loaders. Do not publish binaries containing AES secrets;
  CI artifact builds explicitly disable `SUF2_DECRYPTION_KEYS`. Certificates are example
  containers, not an implemented CA authorization mechanism.
- Platform enforcement is separate from package authentication. RP2040 lacks
  hardware secure boot. Do not automate irreversible RP2350 OTP/security
  provisioning, ESP32-S3 eFuse/flash-encryption provisioning, or flash attached
  hardware without explicit user authorization. Never flash the ESP32-S3 blink
  project's generated bootloader; install its signed package through SUF2BOOT.
- Anti-rollback and encryption at rest are outside the v0.1 design scope.

## Implementing and checking changes

Follow nearby code patterns. Make focused changes, keep dependent call sites
consistent, and surface errors rather than returning success-shaped fallbacks.
Add focused regression tests for changed behavior, not tests that merely
repeat constants or implementation details.

Run the smallest relevant checks first. Commands below assume the repository
root:

```sh
# Python packer
PYTHONPATH=host/python/src python3 -m unittest discover -s host/python/tests -v

# .NET packer and its independent checks
dotnet build host/dotnet -c Release
python3 host/dotnet/selfcheck.py

# Dashboard
npm --prefix host/dashboard run checks
npm --prefix host/dashboard test

# Cross-producer compatibility
PYTHONPATH=host/python/src python3 -m unittest discover -s host/shared/tests -v

# Full host checks (inspect output: missing dotnet/npm may be skipped)
tools/host/run_tests.sh

# Ceedling unit tests (gem install ceedling --no-document first)
tools/firmware/run_unit_tests.sh

# Ceedling, native verifier and loader simulation tests
tools/firmware/build.sh --test-only

# Board builds, without flashing
tools/firmware/build.sh all
# ESP32-S3 requires firmware/lib/esp-idf/install.sh esp32s3 first
tools/firmware/build.sh esp32s3

# Shell syntax and ShellCheck when available
tools/validation/syntax-check-shell.sh
```

Use `tools/firmware/flash.sh --dry-run` for command verification unless a real
flash was requested. Do not present simulation or dry-run results as hardware
validation. Documentation-only changes need no build unless they change a
testable contract.

Portable unit tests live in `firmware/suf2/tests/unit/` and use Ceedling,
Unity and CMock. Use the latest Ceedling release without Gemfile/Bundler or a
version pin; CI installs it on each run. Mock the crypto contract for isolated tests; retain native
tests with real crypto and the board simulation for integration evidence.
Keep generated mocks and reports in ignored `build/ceedling-suf2/`.

Do not develop unit tests for code in `firmware/boards/`: it is demo code.
Firmware unit-test development belongs to reusable code in `firmware/suf2/`,
not the board bootloader, adapters, or blink example. Validate demo changes
with builds and existing integration/simulation checks; retain those existing
checks.

## CI and releases

[PR Check](workflows/pr-check.yml) calls reusable host and firmware validation.
Keep workflow commands aligned with the scripts in `tools/`, pin external
actions to commit SHAs, and use least-privilege workflow permissions.

Follow [Releasing SUF2](../docs/deployment/releasing.md) for release gates,
tag-derived package versions, PyPI, and Pages. Release host tools and the
dashboard, not generic loader binaries: each product needs its own trusted
keys and product ID. Do not publish, tag, deploy, or change repository settings
unless requested.
