# ShareMe Dynamic Luna Model Routing Verification

## Scope and environment

This is dated verification evidence from 2026-08-03 in the ShareMe feature
worktree on macOS. It covers one bounded, filesystem-read-only repository audit
and the quality of its response; it does not rerun ShareMe product behavior on
macOS or Windows.

- Target capability tier: lower-cost balanced read-only exploration.
- Requested and runtime-accepted model: `gpt-5.6-terra`.
- Requested reasoning effort: medium.
- Fallback or difference: none surfaced by the runtime.
- Complete task boundary: inspect the current branch and remote baseline,
  historical Windows evidence, stale README statements, the canonical next
  stage, and the dynamic-routing dispatch contract. The Explorer was forbidden
  to write files, build, test, fetch, or inspect another worktree.

The stable ShareMe skill and role policy remain model-agnostic; this dated
historical verification is the only tracked record here that names the model.

## Comparison against Sol verification

Sol independently compared the Explorer response with current Git and tracked
files. The response matched every acceptance fact, and Git status before and
after the Explorer was unchanged apart from this plan's pre-existing intended
checkbox edit. The zero-write boundary was therefore preserved.

| Checked fact | Verified result |
| --- | --- |
| Branch baseline | `HEAD` was `52c3daaa745260733d0479e5c81bbf68834dd051`; `origin/main` was `0bd3bc5dbbe26a4f8ff45a02b0440a7849010acd`; `HEAD...origin/main` was ahead 5, behind 0. |
| Windows delivery history | Delivery merge `5db696c`, desktop evidence `ec2ddc8`, cross-platform regression merge `ce52e48`, and its evidence commit `ace1be7` were present in Git. |
| Recorded desktop evidence | The 2026-07-31 Windows evidence records 16/16 and 38/38 CTest results, real 4K HDR Desktop Duplication capture, and 22 received real desktop frames. |
| Recorded cross-platform evidence | The 2026-07-31 Windows evidence records 14/14 and 36/36 CTest results plus synthetic and native-microphone movie/audio smoke. |
| README reconciliation | README rows for Windows Qt/FFmpeg playback and Windows libwebrtc are stale; its final Windows-native-media summary is broader than the newer recorded Windows evidence. |
| Next stage | Reconcile the README verification table first, then resume player integration and receiver playback control. |
| Dispatch contract | The exact five routing fields are present and their association is enforced by the workflow contract test. |

## Result and limitations

**Verified — routing:** the fresh Explorer dispatch accepted the explicit
model override and returned a compliant result for the bounded audit.

**Verified — quality:** Sol's comparison found all audited repository facts,
the no-write status, and the stated next stage correct.

**Unmeasured — realized credit saving:** no per-agent token or credit telemetry
was exposed. This run establishes neither an exact credit saving nor a general
claim about model availability outside this dated runtime.

The Windows counts and hardware/two-process outcomes above are historical
recorded evidence. They are not a new Windows execution, a macOS rerun, or
cross-platform product acceptance from this routing task.
