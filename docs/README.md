# SUF2 documentation

## Reusable security references

These documents describe general security considerations. They are intended as stable references, independent of the SUF2 format.

| Document | Purpose |
| --- | --- |
| [reference/threat-model.md](reference/threat-model.md) | Stable, generic threat model for firmware update and secure boot: scope, trust boundaries, assets, attackers, objectives and threats |
| [reference/trust-chain.md](reference/trust-chain.md) | Stable, generic secure-boot trust chain, requirements and assurance labels |
| [reference/secure-boot-lifecycle.md](reference/secure-boot-lifecycle.md) | Stable, generic firmware update and boot decisions, from candidate reception through application handoff |

## SUF2 project documentation

These documents describe the SUF2 profile, its design choices and its implementation guidance.

| Document | Purpose |
| --- | --- |
| [security/security-design.md](security/security-design.md) | What SUF2 v0.1 does about each threat, what it leaves to the platform, and what it accepts |
| [security/platform-integration.md](security/platform-integration.md) | Preliminary SUF2 integration notes for RP2040, RP2350 and future platforms |
| [security/design-guidelines.md](security/design-guidelines.md) | How to build a receiver and loader safely |
| [suf2-specification.md](suf2-specification.md) | SUF2 v0.1 UF2 profile, package and manifest layout, cryptographic construction and receiver rules |
| [conformance.md](conformance.md) | Test-case catalogue for implementations |
| [why-suf2.md](why-suf2.md) | Why SUF2 uses UF2 transport, what it adds, and the design trade-offs |

Suggested reading order: start with [Why SUF2?](why-suf2.md) for the motivation, then read the reusable threat model and trust chain. Continue with the SUF2 security design and specification, followed by the design guidelines and conformance catalogue. Consult platform integration notes when assessing a specific board.
