# Physical-Layer Sybil Defence on ESP32

Detecting spoofed identities in a wireless swarm by studying radio-channel characteristics rather than relying only on message contents.

This project investigates ESP32-based swarm communication using ESP-NOW, sensor-anchored consensus, W-MSR-style neighbour trimming, and physical-layer radio fingerprinting.

**Current status:** Consensus and neighbour trimming have been implemented and tested on a limited number of boards. Full multi-identity Sybil detection and physical-layer defence remain pending.

See `docs/` for the project build guide. Follow the documented workflow:

**Implement -> Build -> Test -> Record evidence -> Commit**

## Repository Layout

- `firmware/common/` - Shared protocol and common definitions
- `firmware/honest_node/` - Honest-node ESP32 firmware
- `firmware/attacker_node/` - Planned attacker firmware and attack experiments
- `dashboard/` - Dashboard and visualisation
- `analysis/` - Experiment analysis, metrics, and plots
- `data/experiments/` - Experiment datasets
- `docs/` - Build guide, conditions logs, and findings

## Toolchain

- ESP-IDF v5.2.x
- Python 3.10+ for dashboard and analysis components

## Development Progress

| Manual step | Feature or experiment | Status |
|---|---|---|
| Step 2 | CSI capability verification | Previously recorded as completed; verify the supporting evidence |
| Step 10 | Sensor-anchored consensus using LDR readings and ESP-NOW | Implemented; tested on a limited board setup |
| Step 11 | W-MSR-style neighbour trimming with `F_PARAM = 1` | Implemented and tested; does not guarantee Sybil resistance |
| Step 12 | Controlled misleading-state test mode | Implemented; disabled by default; CSV telemetry requirements remain to be verified |
| Step 14 | Single-identity forged-state attacker firmware | Pending |
| Step 15 | Multi-identity Sybil attack | Pending |
| Step 16 | Repeated baseline experiments and CSV dataset | Pending |
| Step 17 | Replay attack experiment | Pending |
| Steps 18-25 | Physical-layer measurements and fingerprinting | Pending verification against the manual |
| Steps 26-28 | Sybil detection and defence | Pending |
| Steps 29-33 | Demonstration, evaluation, and reporting | Pending |

## Current Firmware Behaviour

- ESP-NOW broadcast communication between nodes
- LDR sensor readings used to anchor node estimates
- Periodic consensus updates
- W-MSR-style trimming of extreme neighbour estimates
- Compile-time misleading-state test switch, disabled by default

The current trimming implementation is an experimental W-MSR-style mechanism. It does not, by itself, guarantee resistance to multiple forged identities.

## Current Limitations

- The full eight-node configuration has not been verified in the current test session.
- The manual's required CSV telemetry format needs to be verified.
- Separate attacker firmware and its command interface are not yet implemented.
- Multi-identity Sybil and replay experiments are pending.
- Physical-layer fingerprinting, detection, and defence must be validated against the documented experiment plan.

## Development Principle

A feature is marked complete only when its implementation and the required evidence have been verified. Implementation, limited testing, and full experimental validation are recorded separately.