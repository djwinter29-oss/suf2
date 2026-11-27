# SUF2 documentation

## Reusable security references

These documents describe general security considerations. They are intended as stable references, independent of the SUF2 format.

| Document | Purpose |
| --- | --- |
| [reference/security/threat-model.md](reference/security/threat-model.md) | Stable, generic threat model for firmware update and secure boot: scope, trust boundaries, assets, attackers, objectives and threats |
| [reference/security/trust-chain.md](reference/security/trust-chain.md) | Stable, generic secure-boot trust chain, requirements and assurance labels |
| [reference/security/update-lifecycle.md](reference/security/update-lifecycle.md) | Stable, generic firmware-update decisions, interruption safety, boot eligibility and recovery promises |

## SUF2 project documentation

These documents describe the SUF2 profile, its design choices and its implementation guidance.

| Document | Purpose |
| --- | --- |
| [security/security-design.md](security/security-design.md) | What SUF2 v0.1 does about each threat, what it leaves to the platform, and what it accepts |
| [security/platform-integration.md](security/platform-integration.md) | Preliminary SUF2 integration notes for RP2040, RP2350 and future platforms |
| [security/design-guidelines.md](security/design-guidelines.md) | How to build a receiver and loader safely |
| [spec.md](spec.md) | UF2 profile, record and manifest layout, crypto construction, receiver rules |
| [conformance.md](conformance.md) | Test-case catalogue for implementations |
| [related-work.md](related-work.md) | Existing projects, gap, and reuse-versus-new-format options |

Suggested reading order: start with the reusable threat model, trust chain and update lifecycle, then read the SUF2 security design, specification, design guidelines and conformance. Consult platform integration notes when assessing a specific board.
