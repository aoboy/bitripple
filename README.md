# LibBitRipple

**Deterministic Multipath Packet Spraying with RaptorQ Fountain Coding and Closed-Loop Congestion Feedback**

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Component Descriptions](#3-component-descriptions)
4. [Data Flow Diagrams](#4-data-flow-diagrams)
5. [The Whack-a-Mole Algorithm](#5-the-whack-a-mole-algorithm)
6. [Congestion Feedback System](#6-congestion-feedback-system)
7. [RaptorQ vs. Traditional LT Codes](#7-raptorq-vs-traditional-luby-transform-codes)
8. [API Reference](#8-api-reference)
9. [Build System](#9-build-system)
10. [Testing Framework](#10-testing-framework)
11. [Performance Characteristics](#11-performance-characteristics)
12. [Use Cases](#12-use-cases)
13. [Appendices](#13-appendices)

---

## 1. Overview

LibBitRipple is a high-performance C library implementing the **Whack-a-Mole** deterministic packet spraying algorithm, integrated with **RaptorQ** fountain coding via the `nanorq` codec, and featuring a **closed-loop congestion feedback system** for dynamic path quality adaptation.

It is designed for large-scale distributed AI/ML training and inference workloads where **Collective Completion Time (CCT)** and **Effective Training Time Ratio (ETTR)** are critical metrics.

Unlike traditional multipath transports that rely on ECMP flow hashing or stochastic packet spraying, LibBitRipple provides:

- **Deterministic load balancing** with provably tight discrepancy bounds ($O(\log m)$)
- **Erasure-coded reliability** without retransmissions
- **Dynamic congestion adaptation** via real-time feedback (ECN, loss, RTT)
- **Minimal per-packet overhead** suitable for NIC or GPU-resident implementations
- **Closed-loop feedback** from receiver to sender for proactive path degradation response

---

## 2. Architecture

LibBitRipple is organized into a layered architecture separating the **spraying logic**, **erasure coding**, **network transport**, and **feedback control**.

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                         │
│         (Distributed Training, Streaming, etc.)              │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                   LIBBITRIPPLE API                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │   Client    │  │   Server    │  │   Profile Manager   │  │
│  │  (Encoder + │  │  (Decoder + │  │  (Path Allocation + │  │
│  │   Feedback  │  │   Feedback  │  │   Adaptation)       │  │
│  │   Receiver) │  │   Sender)   │  │                     │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
└─────────┼────────────────┼────────────────────┼─────────────┘
          │                │                    │
┌─────────▼────────────────▼────────────────────▼─────────────┐
│                  CORE ENGINE LAYER                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │   Spray     │  │   Profile   │  │      Feedback       │  │
│  │   Engine    │  │   Updater   │  │      Engine         │  │
│  │(Bit-Reversal│  │(Embodiments │  │  (Severity-based    │  │
│  │  Counter)   │  │   1-4)      │  │   Redistribution)   │  │
│  └──────┬──────┘  └──────┬──────┘  └─────────────────────┘  │
└─────────┼────────────────┼──────────────────────────────────┘
          │                │
┌─────────▼────────────────▼──────────────────────────────────┐
│                   CODEC LAYER                                │
│              ┌─────────────────┐                             │
│              │     nanorq      │                             │
│              │  (RaptorQ RFC   │                             │
│              │     6330)       │                             │
│              └─────────────────┘                             │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                  NETWORK LAYER                               │
│  ┌─────────────────┐  ┌─────────────────┐                   │
│  │   Data Plane    │  │  Control Plane  │                   │
│  │   (UDP Data     │  │  (UDP Feedback  │                   │
│  │    Packets)     │  │   Packets)      │                   │
│  └─────────────────┘  └─────────────────┘                   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Component Descriptions

### 3.1 Path Profile (`bitripple_profile.c`)

The **Path Profile** module implements the discrete "balls-into-bins" model described in the Whack-a-Mole paper.

- **Representation**: $n$ paths, $m$ balls where $m = 2^\ell$
- **Invariant**: $\sum_{i=0}^{n-1} b(i) = m$
- **Cumulative Array**: Pre-computed $c(i) = \sum_{j=0}^i b(j)$ for $O(\log n)$ path lookup

**Key Functions:**
- `br_profile_new(n, m)` — Allocate a profile with $n$ paths and $m$ balls
- `br_profile_set_uniform()` — Equal distribution
- `br_profile_set_fractions()` — Weighted distribution from float array
- `br_profile_update_whack()` — Embodiment 1: remove $\alpha$ fraction from one path and redistribute evenly
- `br_profile_update_redistribute_even()` — Embodiment 2: remove from multiple bins, redistribute evenly
- `br_profile_update_redistribute_exclude()` — Embodiment 3: remove from set $K$, redistribute to complement $\overline{K}$
- `br_profile_update_redistribute_proportional()` — Embodiment 4: proportional redistribution minimizing weighted sum

### 3.2 Spray Engine (`bitripple_spray.c`)

The **Spray Engine** implements deterministic packet distribution using bit-reversal permutations.

**Core Mechanism:**
1. Maintain $\ell$-bit counter $j$ (modulo $m$)
2. Compute $\theta(j, \ell)$ by reversing $\ell$ LSBs of $j$
3. Select path $i$ where $c(i-1) \le \theta(j, \ell) < c(i)$

**Shuffle Methods:**
- **Shuffle 1**: $\theta(s_a + j \cdot s_b, \ell)$ — Deviation bound $\le \ell$
- **Shuffle 2**: $(s_a + s_b \cdot \theta(j, \ell)) \bmod 2^\ell$ — Deviation bound $\le 2\ell$

### 3.3 Feedback Engine (`bitripple_feedback.c`)

The **Feedback Engine** implements the closed-loop congestion control system.

**Per-Path State Tracking:**
- `ecn_count[]` — Cumulative ECN markings per path
- `loss_count[]` — Cumulative packet losses per path
- `rtt_ewma[]` — Exponentially weighted moving average of RTT
- `rtt_samples[]` — Number of RTT measurements

**Severity Classification (5 levels):**
| Level | Condition | Action |
|-------|-----------|--------|
| `BR_SEV_NONE` | Normal operation | None |
| `BR_SEV_MILD` | RTT > 2x baseline | Reduce 10% |
| `BR_SEV_MODERATE` | ECN detected | Reduce 25% |
| `BR_SEV_SEVERE` | Packet loss detected | Reduce 50% |
| `BR_SEV_CRITICAL` | ECN + RTT > 3x baseline | Reduce 75-90% |

**Update Methods:**
- `br_feedback_apply_ecn()` — Moderate whack ($\alpha = 0.25$)
- `br_feedback_apply_loss()` — Severe whack ($\alpha = 0.50$ to $0.90$)
- `br_feedback_apply_rtt()` — EWMA-based gradual reduction
- `br_feedback_apply_severity()` — Embodiment 4: weighted objective minimization

### 3.4 Codec Wrapper (`bitripple_codec.c`)

Abstracts the `nanorq` RaptorQ implementation behind a clean encoder/decoder interface.

**Encoder:**
- Creates `nanorq` encoder from memory buffer via `ioctx_from_mem()`
- Pre-generates symbols for all source blocks (SBNs)
- Supports multi-block messages (nanorq partitions large payloads)
- Exposes OTI (Object Transmission Information) for decoder initialization

**Decoder:**
- Lazy initialization from OTI carried in first packet header
- Tracks repair status per block independently
- `br_decoder_ready()` attempts repair on all blocks
- Returns reconstructed data when all blocks decoded

### 3.5 Network Layer (`bitripple_net.c`)

Lightweight UDP socket abstraction:
- `br_udp_socket_create()` — Non-blocking UDP socket
- `br_udp_bind()` — Bind to port
- `br_udp_sendto()` / `br_udp_recvfrom()` — Send/receive with addressing

### 3.6 Client (`bitripple_client.c`)

High-level sender that orchestrates encoding, spraying, and feedback processing:
1. **Pre-send**: Poll feedback socket for pending congestion signals
2. **Encode**: Create RaptorQ encoder, generate symbols for all blocks
3. **Spray**: For each symbol, select path via Whack-a-Mole, attach header with timestamp
4. **Send**: Transmit via UDP to selected path destination
5. **Post-send**: Poll feedback socket again for immediate reactions

**Feedback Reception:**
- Dedicated non-blocking UDP socket bound to `feedback_port`
- `br_client_poll_feedback()` processes incoming `br_feedback_pkt_t` packets
- Applies severity-based profile adjustments in real-time

### 3.7 Server (`bitripple_server.c`)

High-level receiver that reconstructs data and generates feedback:
1. **Listen** on UDP socket for data packets
2. **Parse** headers: extract path_id, path_seq, flags (ECN, RTT)
3. **Detect** anomalies:
   - **Loss**: Gap in `path_seq` sequence numbers
   - **ECN**: `BR_FLAG_ECN` set in header
   - **RTT**: `BR_FLAG_RTT_UPDATE` with measured value
4. **Send Feedback**: Construct `br_feedback_pkt_t` and send to client's feedback port
5. **Decode**: Feed symbols into `nanorq` decoder, repair when sufficient
6. **Return**: Reconstructed data when all blocks ready

**Per-Path Statistics:**
```c
typedef struct br_path_stats {
    uint32_t packets_received;
    uint32_t packets_lost;
    uint32_t last_seq;
    uint32_t ecn_count;
    uint64_t rtt_sum_usec;
    uint32_t rtt_samples;
    uint32_t last_rtt_usec;
} br_path_stats_t;
```

---

## 4. Data Flow Diagrams

### 4.1 Encoding and Transmission Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────────┐
│   User Data │────▶│  br_encoder │────▶│  nanorq_encode  │
│   (Buffer)  │     │    _new()   │     │  (per SBN/ESI)  │
└─────────────┘     └──────┬──────┘     └─────────────────┘
                           │
                           ▼
                    ┌─────────────┐
                    │  Generate   │
                    │  Symbols    │
                    │ (Src+Repair)│
                    │  All Blocks │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │  Symbol 0  │  │  Symbol 1  │  │  Symbol N  │
    │  (SBN=0)   │  │  (SBN=0)   │  │  (SBN=M)   │
    └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
          │               │               │
          ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │   Spray    │  │   Spray    │  │   Spray    │
    │  Select    │  │  Select    │  │  Select    │
    │  Path=P0   │  │  Path=P1   │  │  Path=Pk   │
    └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
          │               │               │
          ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │  Attach    │  │  Attach    │  │  Attach    │
    │  Header    │  │  Header    │  │  Header    │
    │ +Timestamp │  │ +Timestamp │  │ +Timestamp │
    └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
          │               │               │
          ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │  UDP Send  │  │  UDP Send  │  │  UDP Send  │
    │  to P0     │  │  to P1     │  │  to Pk     │
    └────────────┘  └────────────┘  └────────────┘
```

### 4.2 Reception, Decoding, and Feedback Generation Flow

```
┌────────────┐  ┌────────────┐  ┌────────────┐
│ UDP Recv   │  │ UDP Recv   │  │ UDP Recv   │
│ from P0    │  │ from P1    │  │ from Pk    │
└─────┬──────┘  └─────┬──────┘  └─────┬──────┘
      │               │               │
      └───────────────┼───────────────┘
                      ▼
              ┌─────────────┐
              │ Parse Header│
              │ (SBN,ESI,   │
              │  path_seq,  │
              │  flags)     │
              └──────┬──────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
 ┌──────────┐  ┌──────────┐  ┌──────────┐
 │  Check   │  │  Check   │  │  Check   │
 │  Loss:   │  │  ECN:    │  │  RTT:    │
 │  gap in  │  │  flag    │  │  field   │
 │  path_seq│  │  set?    │  │  present?│
 └────┬─────┘  └────┬─────┘  └────┬─────┘
      │             │             │
      ▼             ▼             ▼
 ┌──────────┐  ┌──────────┐  ┌──────────┐
 │ Build    │  │ Build    │  │ Build    │
 │ Feedback │  │ Feedback │  │ Feedback │
 │ (LOSS)   │  │ (ECN)    │  │ (RTT)    │
 └────┬─────┘  └────┬─────┘  └────┬─────┘
      │             │             │
      └─────────────┼─────────────┘
                    ▼
            ┌─────────────┐
            │ UDP sendto()│
            │ to client   │
            │ feedback port
            └─────────────┘
                    │
                    ▼
            ┌─────────────┐
            │ br_decoder  │
            │  _feed()    │
            │ (per block) │
            └──────┬──────┘
                   │
                   ▼
            ┌─────────────┐
            │ nanorq_add  │
            │  _symbol()  │
            └──────┬──────┘
                   │
                   ▼
            ┌─────────────┐
            │  All Blocks │
            │  Repairable?│
            └──────┬──────┘
                   │
            ┌──────┴──────┐
            │             │
            ▼             ▼
      ┌─────────┐   ┌─────────┐
      │   NO    │   │   YES   │
      │ Continue│   │ Reconstruct│
      │ Receiving│  │ & Return  │
      └─────────┘   └─────────┘
```

### 4.3 Closed-Loop Feedback Control Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                         CLIENT                               │
│                                                              │
│  ┌─────────────┐         ┌─────────────┐                    │
│  │   Profile   │◄────────│   Feedback  │                    │
│  │   State     │  adjust │   Engine    │                    │
│  │  b[0..n-1]  │         │  (severity- │                    │
│  │  cum[]      │         │   weighted) │                    │
│  └──────┬──────┘         └──────▲──────┘                    │
│         │                       │                           │
│         │    ┌──────────────────┘                           │
│         │    │                                               │
│         │    │    ┌─────────────────┐                      │
│         │    └────┤  Poll feedback  │                      │
│         │         │  socket (non-   │                      │
│         │         │   blocking)     │                      │
│         │         └─────────────────┘                      │
│         │                       ▲                          │
│         │                       │ br_feedback_pkt_t        │
│         ▼                       │                          │
│  ┌─────────────┐         ┌─────┴──────┐                   │
│  │   Spray     │         │  Feedback  │                   │
│  │   Engine    │         │  Socket    │                   │
│  │  (select    │         │  (recvfrom)│                   │
│  │   path)     │         │            │                   │
│  └──────┬──────┘         └────────────┘                   │
│         │                                                   │
│         ▼                                                   │
│  ┌─────────────┐                                            │
│  │  UDP Send   │────────────────────────────────────────────┼──►
│  │  (data pkt) │                                            │   │
│  └─────────────┘                                            │   │
│                                                             │   │
└─────────────────────────────────────────────────────────────┘   │
                                                                  │
                              ┌───────────────────────────────────┘
                              │ feedback packets
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                         SERVER                               │
│                                                              │
│  ┌─────────────┐                                            │
│  │  UDP Recv   │◄───────────────────────────────────────────┘
│  │  (data pkt) │
│  └──────┬──────┘
│         │
│         ▼
│  ┌─────────────────────────────────────────┐
│  │  Per-Path Statistics                    │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐   │
│  │  │ path 0  │ │ path 1  │ │ path n  │   │
│  │  │ seq=42  │ │ seq=38  │ │ seq=41  │   │
│  │  │ ecn=2   │ │ ecn=0   │ │ ecn=1   │   │
│  │  │ lost=1  │ │ lost=0  │ │ lost=2  │   │
│  │  └─────────┘ └─────────┘ └─────────┘   │
│  └─────────────────────────────────────────┘
│         │
│         ▼
│  ┌─────────────────────────────────────────┐
│  │  Anomaly Detection                      │
│  │  ┌─────────────────────────────────┐    │
│  │  │ if (seq_gap)  → SEVERE (loss)   │    │
│  │  │ if (ecn_flag) → MODERATE (ECN)  │    │
│  │  │ if (rtt_high) → MILD/CRITICAL   │    │
│  │  └─────────────────────────────────┘    │
│  └─────────────────────────────────────────┘
│         │
│         ▼
│  ┌─────────────────────────────────────────┐
│  │  Build br_feedback_pkt_t                │
│  │  ┌─────────────────────────────────┐    │
│  │  │ magic, path_id, severity,       │    │
│  │  │ flags, lost_seq, lost_count,    │    │
│  │  │ rtt_usec                        │    │
│  │  └─────────────────────────────────┘    │
│  └─────────────────────────────────────────┘
│         │
│         ▼
│  ┌─────────────┐
│  │  UDP Send   │──────────────────────────────────────────────►
│  │  (feedback) │                                              │
│  └─────────────┘                                              │
│                                                               │
│  ┌─────────────────────────────────────────┐                  │
│  │  br_decoder_feed() → nanorq_add_symbol  │                  │
│  │  br_decoder_ready() → nanorq_repair     │                  │
│  └─────────────────────────────────────────┘                  │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

### 4.4 Component Interaction Diagram

```
                    ┌─────────────────┐
                    │   Application   │
                    └────────┬────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
        ▼                    ▼                    ▼
 ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
 │   Client    │     │   Server    │     │   Profile   │
 │  (Sender +  │     │ (Receiver + │     │  (Control)  │
 │   Feedback  │◄────┤  Feedback   │     │             │
 │   Consumer) │     │  Producer)  │     │             │
 └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
        │                   │                   │
        │    ┌──────────────┘                   │
        │    │                                  │
        ▼    ▼                                  ▼
 ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
 │   Encoder   │     │   Decoder   │     │    Spray    │
 │  (nanorq)   │◄───►│  (nanorq)   │     │   Engine    │
 └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
        │                   │                   │
        │                   │                   │
        ▼                   ▼                   ▼
 ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
 │   Network   │     │   Network   │     │   Feedback  │
 │   (UDP)     │────►│   (UDP)     │     │   Engine    │
 │  Data Port  │     │  Data Port  │     │  (Severity  │
 └─────────────┘     └─────────────┘     │  classifier)│
                                         └─────────────┘
                                                  ▲
                                                  │
                                         ┌────────┘
                                         │
                                    ┌────┴────┐
                                    │ Feedback│
                                    │ Socket  │
                                    │(Control)│
                                    └─────────┘
```

---

## 5. The Whack-a-Mole Algorithm

### 5.1 Discrete Path Profiles

Instead of continuous probabilities, Whack-a-Mole uses integer allocations:

$$p(i) = \frac{b(i)}{m}, \quad \sum_{i=0}^{n-1} b(i) = m$$

Where $m = 2^\ell$ is the precision. This avoids floating-point drift and enables exact arithmetic.

### 5.2 Bit-Reversal Permutation

The spray counter $j$ increments modulo $m$. The selection value is:

$$\theta(j, \ell) = \text{reverse}_\ell(j \bmod 2^\ell)$$

**Example** ($\ell=4, j=3$):
- Binary: $0011$
- Reversed: $1100 = 12$
- Path selected by binary search on cumulative array

### 5.3 Discrepancy Bounds

For any contiguous packet sequence and any path $i$:

| Method | Deviation Bound |
|--------|----------------|
| Shuffle 1 | $\le \ell = \log_2 m$ |
| Shuffle 2 | $\le 2\ell = 2\log_2 m$ |

For $m=1024$ ($\ell=10$), the maximum deviation is **10 packets** — exponentially better than random spraying.

### 5.4 Dynamic Adaptation ("Whack-a-Mole")

When path $j$ shows degradation:
1. Remove $e(j) = \alpha \cdot b(j)$ balls
2. Redistribute according to selected embodiment
3. Maintain invariant $\sum b(i) = m$

**Four Embodiments:**

| Embodiment | Description | Use Case |
|------------|-------------|----------|
| 1 | Remove from one bin, redistribute evenly to all | Single path degradation |
| 2 | Remove from multiple bins, redistribute evenly to all | Multiple paths degraded |
| 3 | Remove from set $K$, redistribute to complement $\overline{K}$ | Exclude severely degraded paths |
| 4 | Remove from set $K$, redistribute proportionally to all | Weighted optimization per severity |

**Residual Handling:**
- $x = e(j) \div n$ (base redistribution)
- $y = e(j) \bmod n$ (residual)
- Round-robin residuals using global index $r$

---

## 6. Congestion Feedback System

### 6.1 Design Philosophy

The feedback system implements **receiver-driven congestion control** where the server (receiver) detects path quality degradation and notifies the client (sender) to adjust its path profile.

**Key Principles:**
- **Proactive**: Server sends feedback immediately upon anomaly detection
- **Lightweight**: Feedback packets are small (28 bytes) and sent via UDP
- **Severity-graded**: Five severity levels map to different reduction factors
- **EWMA-smoothed**: RTT measurements use exponential smoothing to avoid oscillation

### 6.2 Feedback Packet Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             MAGIC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           PATH_ID                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          PATH_SEQ                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     FLAGS     |   SEVERITY    |           RESERVED            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          RTT_USEC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          LOST_SEQ                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         LOST_COUNT                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Total Size**: 28 bytes

### 6.3 Anomaly Detection

**Loss Detection:**
```
expected_seq = last_seq[path] + 1
if (received_seq > expected_seq):
    lost_count = received_seq - expected_seq
    send_feedback(path, SEVERE, lost_seq=expected_seq, lost_count)
```

**ECN Detection:**
```
if (header.flags & BR_FLAG_ECN):
    stats[path].ecn_count++
    if (ecn_count >= 3):  // debounce
        send_feedback(path, MODERATE)
```

**RTT Detection:**
```
if (header.flags & BR_FLAG_RTT_UPDATE):
    ewma = alpha * rtt + (1-alpha) * ewma
    if (ewma > 2 * baseline):
        send_feedback(path, MILD or CRITICAL)
```

### 6.4 Severity-to-Action Mapping

| Severity | Condition | Alpha | Redistribution |
|----------|-----------|-------|----------------|
| `NONE` | Normal | 0.0 | None |
| `MILD` | RTT > 2x baseline | 0.10 | Even to all |
| `MODERATE` | ECN detected | 0.25 | Even to all |
| `SEVERE` | Packet loss | 0.50 | Even to all |
| `CRITICAL` | ECN + RTT > 3x baseline | 0.75-0.90 | Exclude path |

### 6.5 Embodiment 4: Weighted Objective Minimization

The most sophisticated update method minimizes the weighted sum:

$$\min \sum_{i=0}^{n-1} w(i) \cdot b(i)$$

Where $w(i)$ is derived from severity:
- NONE: $w=1.0$, MILD: $w=2.0$, MODERATE: $w=4.0$, SEVERE: $w=8.0$, CRITICAL: $w=16.0$

The solution redistributes balls **inversely proportional** to weights, favoring healthy paths.

### 6.6 Feedback Loop Timing

```
Time ─────────────────────────────────────────────────────────►

Client:  [Send pkt 0]  [Send pkt 1]  [Send pkt 2]  [Send pkt 3]
              │              │              │              │
              ▼              ▼              ▼              ▼
Path 0:  [Pkt 0 arrives]              [Pkt 2 arrives]
              │                              │
              ▼                              ▼
Server:  [Detect: OK]               [Detect: seq gap!]
                                              │
                                              ▼
                                    [Send feedback: SEVERE]
                                              │
                                              ▼
Client:                              [Poll feedback socket]
                                              │
                                              ▼
                                    [Apply whack: alpha=0.5]
                                              │
                                              ▼
                                    [Reduce path 0 allocation]
```

---

## 7. RaptorQ vs. Traditional Luby Transform Codes

LibBitRipple uses **RaptorQ** (RFC 6330) via `nanorq` rather than traditional LT (Luby Transform) codes. The advantages are substantial:

### 7.1 Systematic Encoding

| Feature | LT Codes | RaptorQ |
|---------|----------|---------|
| **Systematic** | No | **Yes** — first $K$ symbols are the original data |
| **Overhead** | 5-10% | **0.1-1%** |
| **Decoding complexity** | $O(K \cdot \log K)$ | **$O(K)$** linear |
| **Symbol distribution** | Robust Soliton | **Optimized degree distribution** |
| **Block size limit** | Unlimited (theoretically) | **$K \le 56403$** (practical) |
| **Pre-coding** | None | **LDPC + HDPC** inner codes |

### 7.2 Pre-Coding Advantage

RaptorQ employs a two-stage encoding:
1. **Pre-code**: Apply LDPC (Low-Density Parity Check) and HDPC (High-Density Parity Check) codes to source symbols
2. **LT-code**: Apply LT-style encoding to the pre-coded symbols

This **pre-coding** ensures:
- **Linear-time decoding** using belief propagation + Gaussian elimination on a small matrix
- **Near-optimal reception**: Only $K + 2$ symbols needed on average (vs. $1.05K$ to $1.5K$ for LT)
- **Deterministic success probability**: $> 99.99\%$ with just 2-3 extra symbols

### 7.3 Performance Comparison

```
Symbols Required to Decode (K=1000)
│
│    LT Codes          RaptorQ
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓    ▓▓▓▓▓▓▓▓▓▓
│    1050-1500          1002-1005
│
└───────────────────────────────────────

Decoding Time (single core, K=10000)
│
│    LT Codes          RaptorQ
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓    ▓▓▓▓▓▓
│    ~50 ms             ~5 ms
│
└───────────────────────────────────────
```

### 7.4 Why nanorq Specifically

`nanorq` is a compact, high-performance C implementation of RaptorQ:
- **Multi-gigabit speeds** on a single core
- **Flexible I/O**: Memory buffers, mmap, streams
- **Block partitioning**: Automatically splits large transfers into manageable blocks
- **No external dependencies** beyond `oblas` (included as submodule)

### 7.5 Suitability for Multipath Transport

Traditional TCP requires in-order delivery and retransmits lost packets on the same path. In multipath scenarios:
- **TCP**: Head-of-line blocking, timeout escalation, poor utilization of healthy paths
- **LT Codes**: Good, but high overhead and decoding cost
- **RaptorQ**: Optimal — minimal overhead, linear decode, no retransmissions needed, naturally handles out-of-order arrival from multiple paths

---

## 8. API Reference

### 8.1 Lifecycle

```c
int br_init(void);
void br_cleanup(void);
```

### 8.2 Path Profile

```c
br_profile_t* br_profile_new(uint32_t n_paths, uint32_t m_balls);
void br_profile_free(br_profile_t* p);
int br_profile_set_uniform(br_profile_t* p);
int br_profile_set_fractions(br_profile_t* p, const double* frac);

/* Update embodiments */
int br_profile_update_whack(br_profile_t* p, uint32_t path, double alpha);
int br_profile_update_redistribute_even(br_profile_t* p, 
                                        const uint32_t* e, uint32_t n);
int br_profile_update_redistribute_exclude(br_profile_t* p,
                                           const uint32_t* e, uint32_t n);
int br_profile_update_redistribute_proportional(br_profile_t* p,
                                                const uint32_t* e, uint32_t n);
```

### 8.3 Spray Engine

```c
br_spray_t* br_spray_new(uint32_t l, uint32_t sa, uint32_t sb, int method);
void br_spray_free(br_spray_t* s);
uint32_t br_spray_select(const br_profile_t* p, br_spray_t* s, uint32_t j);
uint32_t br_spray_next(br_profile_t* p, br_spray_t* s);
```

### 8.4 Feedback Engine

```c
br_feedback_t* br_feedback_new(br_profile_t* profile, uint32_t n_paths);
void br_feedback_free(br_feedback_t* fb);

br_severity_t br_feedback_classify(uint32_t ecn_count, uint32_t loss_count,
                                   uint32_t rtt_usec, uint32_t baseline_usec);
int br_feedback_apply_severity(br_feedback_t* fb, 
                               const br_severity_t* severity);
int br_feedback_apply_ecn(br_feedback_t* fb, uint32_t path);
int br_feedback_apply_loss(br_feedback_t* fb, uint32_t path, 
                           uint32_t lost_count);
int br_feedback_apply_rtt(br_feedback_t* fb, uint32_t path,
                          uint32_t rtt_usec, uint32_t baseline_usec);
```

### 8.5 Codec

```c
br_encoder_t* br_encoder_new(const void* data, size_t len, uint16_t symbol_size);
void br_encoder_free(br_encoder_t* e);
uint32_t br_encoder_num_blocks(br_encoder_t* e);
uint32_t br_encoder_block_symbols(br_encoder_t* e, uint8_t sbn);
int br_encoder_symbol(br_encoder_t* e, uint8_t sbn, uint32_t esi,
                      void* buf, size_t* out_len);
void br_encoder_oti(br_encoder_t* e, uint64_t* common, uint32_t* scheme);

br_decoder_t* br_decoder_new(uint64_t oti_common, uint32_t oti_scheme,
                             size_t data_len);
void br_decoder_free(br_decoder_t* d);
int br_decoder_feed(br_decoder_t* d, uint8_t sbn, uint32_t esi,
                    const void* buf, size_t len);
int br_decoder_ready(br_decoder_t* d);
int br_decoder_reconstruct(br_decoder_t* d, void* out, size_t len);
```

### 8.6 Network (Client with Feedback)

```c
br_client_t* br_client_new(const br_profile_t* prof,
                           const char** hosts,
                           const uint16_t* ports,
                           uint32_t n_paths,
                           uint16_t symbol_size,
                           uint16_t feedback_port);
void br_client_free(br_client_t* c);
int br_client_send(br_client_t* c, const void* data, size_t len);
int br_client_poll_feedback(br_client_t* c, int timeout_ms);
```

### 8.7 Network (Server with Feedback)

```c
br_server_t* br_server_new(uint16_t port, size_t expected_len, 
                           uint16_t symbol_size,
                           const char* client_host,
                           uint16_t client_feedback_port);
void br_server_free(br_server_t* s);
int br_server_recv(br_server_t* s, void* out, size_t len, int timeout_ms);
```

---

## 9. Build System

### 9.1 Directory Layout

```
libbitripple/
├── Makefile
├── include/
│   ├── bitripple.h
│   └── bitripple_types.h
├── src/
│   ├── bitripple.c
│   ├── bitripple_profile.c
│   ├── bitripple_spray.c
│   ├── bitripple_codec.c
│   ├── bitripple_net.c
│   ├── bitripple_client.c
│   ├── bitripple_server.c
│   ├── bitripple_feedback.c
│   └── bitripple_internal.h
├── tests/
│   ├── test_library.py
│   └── test_library_net.py
└── nanorq/          (git submodule)
    ├── Makefile
    ├── include/
    ├── lib/
    └── deps/oblas/
```

### 9.2 Build Targets

| Target | Output | Description |
|--------|--------|-------------|
| `libnanorq.a` | Static archive | nanorq + oblas objects |
| `libnanorq.so` | Shared library | nanorq + oblas (PIC) |
| `libbitripple.a` | Static archive | Core library with feedback |
| `libbitripple.so` | Shared library | Core library + nanorq linkage |
| `test` | — | Run Python unit and integration tests |
| `clean` | — | Remove all build artifacts |

### 9.3 Compilation Flags

```makefile
CPPFLAGS = -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS   = -O3 -g -std=c99 -Wall -fPIC
```

The `oblas` dependency is compiled with:
```makefile
CPPFLAGS += -DOBLAS_AVX -DOCTMAT_ALIGN=32
```

---

## 10. Testing Framework

### 10.1 Unit Tests (`test_library.py`)

| Test | Description |
|------|-------------|
| `test_profile` | Verify uniform profile sums to $m$ |
| `test_spray` | Verify exact 50/50 distribution over 256 packets |
| `test_codec_roundtrip` | Encode data, generate symbols for all blocks, decode, verify equality |
| `test_feedback_embodiment1` | Apply ECN feedback, verify path allocation reduced |
| `test_feedback_embodiment4` | Apply severity-weighted update, verify weighted redistribution |

### 10.2 Integration Tests (`test_library_net.py`)

| Test | Description |
|------|-------------|
| `test_client_server` | Spawn server thread, send via client over UDP localhost, verify reconstruction |
| `test_feedback_loop` | Simulate loss on one path, verify feedback packet received and profile adjusted |
| `test_ecn_adaptation` | Set ECN flag on packets, verify server sends ECN feedback and client reduces allocation |

### 10.3 Running Tests

```bash
make test
# Or manually:
LD_LIBRARY_PATH=. python3 tests/test_library.py
LD_LIBRARY_PATH=. python3 tests/test_library_net.py
```

---

## 11. Performance Characteristics

### 11.1 Whack-a-Mole Overhead

| Operation | Complexity |
|-----------|------------|
| Path selection (binary search) | $O(\log n)$ |
| Bit-reversal | $O(\ell) = O(\log m)$ |
| Profile update (Embodiment 1) | $O(n)$ |
| Profile update (Embodiment 4) | $O(n)$ |
| Per-packet total | $O(\log n + \log m)$ |

For $n=8$ paths and $m=1024$, per-packet overhead is **~20 integer operations**.

### 11.2 Feedback Overhead

| Metric | Value |
|--------|-------|
| Feedback packet size | 28 bytes |
| Feedback frequency | Event-driven (only on anomalies) |
| Typical feedback rate | < 1% of data packets |
| Processing latency | < 1 microsecond per packet |

### 11.3 RaptorQ Throughput (via nanorq)

| Packet Size | K | Encode | Precalc | Decode |
|-------------|---|--------|---------|--------|
| 1280 B | 100 | ~5.4 Gb/s | ~10 Gb/s | ~5.6 Gb/s |
| 1280 B | 1000 | ~4.6 Gb/s | ~7.8 Gb/s | ~4.7 Gb/s |
| 1280 B | 10000 | ~2.8 Gb/s | ~3.9 Gb/s | ~2.9 Gb/s |

*Measured on Intel i5-8400 @ 2.80GHz*

### 11.4 Scalability

- **Paths**: Up to `BR_MAX_PATHS` (32) without code change
- **Precision**: Up to $m = 2^{31}$ (31-bit counters)
- **Message Size**: Limited by nanorq (~881 GB theoretical)
- **Blocks**: Automatic partitioning by nanorq

---

## 12. Use Cases

### 12.1 Distributed AI/ML Training

- **AllReduce/AllGather** operations across GPU nodes
- Minimize CCT by spraying across multiple network paths
- RaptorQ eliminates retransmission stalls
- Feedback loop rapidly adapts to incast congestion

### 12.2 High-Latency Data Transfer

- Satellite or transcontinental links
- Send repair symbols proactively instead of waiting for ACKs
- Time-varying profiles adapt to diurnal congestion patterns
- Feedback allows graceful degradation of lossy paths

### 12.3 Datacenter Fabric

- Multi-homed servers with multiple ToR uplinks
- ECMP hash collision avoidance via deterministic spraying
- Fast failover when links degrade (feedback-driven)
- No head-of-line blocking from TCP

### 12.4 Firmware/Software Updates

- Broadcast to thousands of devices
- Each device receives different random subset of symbols
- No need for coordinated retransmission
- Feedback allows server to optimize global distribution

---

## 13. Appendices

### Appendix A: Network Packet Formats

#### Data Packet Header (40 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             MAGIC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           BLOCK_ID                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          SYMBOL_ID                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           PATH_ID                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          PATH_SEQ                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  SYMBOL_SIZE  |     FLAGS     |           RESERVED            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         OTI_COMMON (high)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         OTI_COMMON (low)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         OTI_SCHEME                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          RTT_USEC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                         SYMBOL PAYLOAD                        +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Magic Value**: `0x4252504C` ('BRPL')

**Flags:**
- `BR_FLAG_ECN (0x0001)`: ECN experienced congestion
- `BR_FLAG_LOSS (0x0002)`: Gap detected (set by sender if retransmit)
- `BR_FLAG_RTT_UPDATE (0x0004)`: RTT measurement included

#### Feedback Packet (28 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             MAGIC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           PATH_ID                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          PATH_SEQ                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     FLAGS     |   SEVERITY    |           RESERVED            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          RTT_USEC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          LOST_SEQ                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         LOST_COUNT                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Appendix B: Mathematical Notation Summary

| Symbol | Meaning |
|--------|---------|
| $n$ | Number of paths |
| $m$ | Total balls (precision), $m = 2^\ell$ |
| $b(i)$ | Balls allocated to path $i$ |
| $c(i)$ | Cumulative count up to path $i$ |
| $\ell$ | Bit precision, $\ell = \log_2 m$ |
| $j$ | Packet counter |
| $\theta(j, \ell)$ | Bit-reversed $\ell$-bit value |
| $\alpha$ | Whack reduction factor |
| $w(i)$ | Severity weight for path $i$ |
| $K$ | Number of source symbols |
| SBN | Source Block Number |
| ESI | Encoding Symbol ID |
| OTI | Object Transmission Information |
| RTT | Round-Trip Time |
| ECN | Explicit Congestion Notification |
| EWMA | Exponentially Weighted Moving Average |

### Appendix C: State Machine Diagram

```
┌─────────────┐
│   IDLE      │
│  (no data)  │
└──────┬──────┘
       │ br_client_send()
       ▼
┌─────────────┐     ┌─────────────┐
│  ENCODING   │────►│  SPRAYING   │
│  (nanorq    │     │  (select    │
│   generate) │     │   paths)    │
└─────────────┘     └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌─────────┐  ┌─────────┐  ┌─────────┐
        │ Path 0  │  │ Path 1  │  │ Path n  │
        │ (send)  │  │ (send)  │  │ (send)  │
        └────┬────┘  └────┬────┘  └────┬────┘
             │            │            │
             └────────────┼────────────┘
                          ▼
                   ┌─────────────┐
                   │ POLL FEEDBACK│◄─────┐
                   │  (non-block) │      │
                   └──────┬──────┘      │
                          │             │
              ┌───────────┴───────────┐ │
              ▼                       ▼ │
        ┌──────────┐            ┌──────────┐
        │ Feedback │            │ No       │
        │ Received │            │ Feedback │
        └────┬─────┘            └────┬─────┘
             │                       │
             ▼                       │
        ┌──────────┐                 │
        │ Apply    │                 │
        │ Whack    │                 │
        │ (update  │                 │
        │  profile)│                 │
        └────┬─────┘                 │
             │                       │
             └───────────┬───────────┘
                         ▼
                  ┌─────────────┐
                  │  All Sent?  │
                  └──────┬──────┘
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
        ┌──────────┐          ┌──────────┐
        │   NO     │          │   YES    │
        │ (continue│          │ (return  │
        │  sending)│          │  OK)     │
        └──────────┘          └──────────┘
```

### Appendix D: Congestion Response Quick Reference

```
┌─────────────────────────────────────────────────────────────┐
│                    CONGESTION RESPONSE MATRIX                │
├─────────────┬─────────────────┬─────────────────────────────┤
│   Signal    │   Detection     │        Response             │
├─────────────┼─────────────────┼─────────────────────────────┤
│ Single ECN  │ flags & 0x01    │ br_feedback_apply_ecn()     │
│             │                 │ → alpha = 0.25              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ Burst ECN   │ ecn_count >= 3  │ br_feedback_apply_ecn()     │
│             │                 │ → alpha = 0.50              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ 1-5 losses  │ seq_gap > 0     │ br_feedback_apply_loss()    │
│             │                 │ → alpha = 0.50              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ 5-10 losses │ seq_gap > 5     │ br_feedback_apply_loss()    │
│             │                 │ → alpha = 0.75              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ 10+ losses  │ seq_gap > 10    │ br_feedback_apply_loss()    │
│             │                 │ → alpha = 0.90              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ RTT 2x base │ ewma > 2x       │ br_feedback_apply_rtt()     │
│             │                 │ → alpha = 0.10              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ RTT 4x base │ ewma > 4x       │ br_feedback_apply_rtt()     │
│             │                 │ → alpha = 0.30              │
├─────────────┼─────────────────┼─────────────────────────────┤
│ ECN + RTT   │ both conditions │ br_feedback_apply_severity()│
│             │                 │ → CRITICAL: exclude path    │
└─────────────┴─────────────────┴─────────────────────────────┘
```

---

*Document Version: 2.0*
*Updated with complete congestion feedback system implementation*
*Generated for LibBitRipple — Deterministic Multipath Transport with RaptorQ and Closed-Loop Adaptation*
