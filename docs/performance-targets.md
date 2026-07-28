# ShareMe Performance Targets

## First-Release Targets

| Metric | Target |
| --- | --- |
| default movie stream | 1920x1080, 60 fps, SDR |
| direct voice latency | P50 at most 120 ms; P95 at most 200 ms |
| TURN voice latency | aim for at most 250 ms |
| low-latency movie path | 150-300 ms end to end |
| high-quality movie path | 250-500 ms end to end |
| movie A/V error | at most 50 ms |
| stable host/viewer timeline error | at most 100 ms |
| stable-network video drop rate | at most 1% |
| reconnect attempt | starts within 5 seconds |
| movie audio | 48 kHz stereo Opus, 128-192 kbit/s |
| voice audio | 48 kHz mono Opus, 32-64 kbit/s |

These are acceptance targets, not results. A target becomes verified only after
the measurement procedure below has produced evidence.

## Measurement Definitions

| Metric | Start | End | Required context |
| --- | --- | --- | --- |
| connection establishment | create/join accepted | all required tracks connected |
| video encode time | frame submitted | encoded frame callback |
| movie latency | host source PTS presentation instant | viewer display instant |
| voice latency | acoustic/digital marker at capture | rendered marker at peer |
| A/V error | rendered movie audio PTS | rendered movie video PTS |
| timeline error | host rendered movie PTS | viewer rendered movie PTS |
| drop rate | frames intentionally submitted | frames rendered |
| RTT/jitter/loss | WebRTC stats sample | same sample interval |
| reconnect time | transport failure detected | media flow restored |

Use monotonic clocks within one machine. Cross-machine latency measurements need
a shared external marker or measured clock-offset method; wall clocks alone are
not accepted evidence.

## Test Profiles

Every network result names one profile:

| Profile | RTT | Packet loss | Bandwidth |
| --- | ---: | ---: | ---: |
| clean LAN | below 20 ms | 0% | at least 100 Mbit/s |
| typical WAN | 60 ms | 1% | 25 Mbit/s upstream |
| constrained | 120 ms | 3% | 12 Mbit/s upstream |
| recovery | changes 20-200 ms | changes 0-10% | step down and restore |
| TURN | measured | measured | relay path confirmed |

Network shaping configuration, host/viewer hardware, OS build, GPU driver,
codec, bitrate, resolution, frame rate, route type, and test duration accompany
every report.

## Resource and Stability Reporting

Report:

- process CPU average and peak;
- GPU engine utilization and encoder utilization when available;
- working-set memory and growth;
- queue high-water marks and drop counters;
- encoded and rendered frame counts;
- underrun, overrun, freeze, reconnect, and hard-resync counts.

Short functional tests run at least five minutes. Stability gates use two-hour
and eight-hour runs. Memory growth is evaluated after warm-up, not from process
startup.

## Evidence Status

Each result is labeled:

- **verified**: exact procedure completed and raw evidence retained;
- **partial**: only part of the path or duration was measured;
- **unverified**: implementation or target exists without a valid measurement;
- **environment-bound**: the required OS, hardware, network, or remote peer was
  unavailable.

The foundation slice contains targets and measurement contracts only. It makes
no media performance claim.
