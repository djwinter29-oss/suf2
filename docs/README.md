# SUF2 documentation

For runnable host prototypes, see [host tools](../host/README.md): Python and
.NET CLI packers, a browser dashboard, and shared key-format examples. The
device side, a C verifier library and an RP2040/RP2350 loader, is in
[firmware](../firmware/README.md).

## Reusable security references

These documents describe general security considerations. They are intended as stable references, independent of the SUF2 format.

| Document | Purpose |
| --- | --- |
| [reference/threat-model.md](reference/threat-model.md) | Stable, generic threat model for firmware update and secure boot: scope, trust boundaries, assets, attackers, objectives and threats |
| [reference/trust-chain.md](reference/trust-chain.md) | Stable, generic secure-boot trust chain, requirements and assurance labels |
| [reference/secure-boot-lifecycle.md](reference/secure-boot-lifecycle.md) | Stable, generic firmware update and boot decisions, from candidate reception through application handoff |

## SUF2 project documentation

| Folder | Document | Purpose |
| --- | --- | --- |
| docs | [why-suf2.md](why-suf2.md) | Why SUF2 uses UF2 transport, what it adds, and the design trade-offs |
| [spec/](spec/) | [suf2-specification.md](spec/suf2-specification.md) | SUF2 v0.1 UF2 profile, package and manifest layout, cryptographic construction and receiver rules |
| [spec/](spec/) | [conformance.md](spec/conformance.md) | Draft portable conformance cases, planned producer checks and platform assurance tests; vectors are future work |
| [security/](security/) | [security-design.md](security/security-design.md) | What SUF2 v0.1 does about each threat, what it leaves to the platform, and what it accepts |
| [implementers/](implementers/) | [design-guidelines.md](implementers/design-guidelines.md) | How to build a receiver and loader safely |
| [implementers/](implementers/) | [platform-integration.md](implementers/platform-integration.md) | Preliminary SUF2 integration notes for RP2040, RP2350 and future platforms |
| [deployment/](deployment/) | [releasing.md](deployment/releasing.md) | What CI checks, how to cut a release, and the repository setup publishing needs |

Suggested reading order: start with [Why SUF2?](why-suf2.md) for the motivation, then read the reusable threat model and trust chain. Continue with the specification and the security design, then the design guidelines and the conformance catalogue. Consult the platform integration notes when assessing a specific board.
