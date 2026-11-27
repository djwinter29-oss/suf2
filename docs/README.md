# SUF2 documentation

| Document | Purpose |
| --- | --- |
| [security/threat-model.md](security/threat-model.md) | Generic threat model for firmware update and secure boot: scope, trust boundaries, assets, attackers, objectives and threats |
| [security/security-design.md](security/security-design.md) | What SUF2 v0.1 does about each threat, what it leaves to the platform, and what it accepts |
| [security/trust-chain.md](security/trust-chain.md) | Generic secure-boot trust chain, requirements and assurance labels |
| [security/platform-integration.md](security/platform-integration.md) | Preliminary SUF2 integration notes for RP2040, RP2350 and future platforms |
| [security/design-guidelines.md](security/design-guidelines.md) | How to build a receiver and loader safely |
| [spec.md](spec.md) | UF2 profile, record and manifest layout, crypto construction, receiver rules |
| [conformance.md](conformance.md) | Test-case catalogue for implementations |
| [related-work.md](related-work.md) | Existing projects, gap, and reuse-versus-new-format options |

Suggested reading order: threat model, trust chain, security design, specification, design guidelines, conformance. Consult platform integration notes when assessing a specific board.
