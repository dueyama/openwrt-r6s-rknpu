# FP16 numerical investigation: a public RK3588 sigmoid LUT

## Result

We reproduced the 1,026 entries of a public RK3588 sigmoid lookup-table generator byte for byte. Applying three explicitly defined CPU interpolation hypotheses to an existing Kitsune-style neural network did **not** reproduce its saved RKNN/NPU anomaly scores exactly: all three produced **0 exact matches out of 257**.

Compared with an earlier generic LUT reference, the largest score difference became about 70% smaller, while the mean absolute difference became 83–93% larger. This is a mixed result, not a general improvement. The actual LUT and conversion settings used inside the current RKNN graphs remain unidentified.

The investigation does not establish a defective NPU, an IEEE floating-point violation, or the detector's ability to identify intrusions. It documents a narrower finding: the public standalone LUT method and the CPU hypotheses below are insufficient to reproduce this particular saved NN execution exactly.

## Why another FP16 reference was needed

Using binary16 inputs and weights does not, by itself, specify a neural network's entire calculation. Accumulator precision, reduction order, bias precision, fused-operation boundaries, activation approximations and output conversions can differ. In particular, a sigmoid implementation can use a table instead of evaluating the analytic exponential expression.

The earlier development check compared the saved NPU score with a higher-precision reference using:

```text
abs(npu_score - reference_score) <= 2e-5 + 2e-5 * abs(reference_score)
```

This was a project-defined FP32 development tolerance, **not an IEEE requirement, a vendor FP16 guarantee, or an operational intrusion-alert threshold**. The earlier 113/257 failures remain recorded as failures of that development check. We did not relax the rule and relabel the run as passing.

## Pinned public sources and what was reproduced

The public implementation is by Greg Ordinary and contributors in [rocket-userspace, commit 1401c46](https://github.com/gregordinary/rocket-userspace/tree/1401c46e75e45adee66b08b97a62e633306de016). We inspected the [activation source](https://github.com/gregordinary/rocket-userspace/blob/1401c46e75e45adee66b08b97a62e633306de016/src/rocket_activation.c), [register-command construction](https://github.com/gregordinary/rocket-userspace/blob/1401c46e75e45adee66b08b97a62e633306de016/src/npu_regcmd.c), and [activation test](https://github.com/gregordinary/rocket-userspace/blob/1401c46e75e45adee66b08b97a62e633306de016/tests/activation_lut_rocket.c). Related [reverse-engineering notes](https://github.com/gregordinary/rockchip-npu-notes/blob/b4efde2a0a083bbed2f074ffdf3343bcd8a92cb3/encodings/dpu-lut-activation.md) were pinned to commit `b4efde2a0a083bbed2f074ffdf3343bcd8a92cb3`.

These are unofficial sources, with AI-authorship disclosures and no accuracy guarantee. Their standalone implementation is evidence for a testable mathematical hypothesis, not a verified description of every RKNN execution.

The inspected sigmoid path has two 513-entry Q0.15 tables. Its generation uses double-precision sigmoid, C `lround`, a step of `32/2596`, and a clamp to `[0,32767]`. The standalone index range is `[-16384,16384]`, about ±6.31 in input units; the output conversion uses offset +1 and shift 15.

A small, pure C table-builder excerpt and an independent Python mathematical reference produced the same 2,052 bytes, encoded as little-endian unsigned 16-bit values, LE table followed by LO table:

```text
SHA-256: 47363f344838cb18c0f36f4d320e6af4d6ca0eb7bb8476141be4b0700d36d422
```

Only the CPU table excerpt was executed. The public driver/device library was not executed. This confirms table generation, not the original author's hardware measurements or our NN's internal activation arithmetic.

## Three hypotheses fixed before comparison

The CPU reference first rounds the activation input to binary16, widens it, and models index scaling in FP32. It then applies linear interpolation in the public table with one of three Q-domain policies:

1. Preserve the real interpolated Q value.
2. Round it down to an integer Q value.
3. Round it to the nearest integer Q value, ties to even.

Intermediate interpolation precision and rounding are hypotheses. The public source does not establish the corresponding hardware contract. Input rounding at fused internal stages is also unconfirmed.

The CPU function rejects inputs outside the table range by default. For the full-NN comparison only, we explicitly used **endpoint saturation as an additional hypothesis**. We did not decode the RK3588 slope registers, assume that generic NVDLA slope encoding applies unchanged, or tune the range, offset or rounding to fit the saved results.

## Fixed-NN comparison

The comparison reused 257 saved NPU results and their matching inputs, model and FP32 port values. All new work was on the CPU. Inputs and weights were rounded to half, the original FP32 bias was retained, and the same lane-16 FP32 reduction, known output half-logit boundary, and original higher-precision normalization/RMSE were used. The model, runtime, driver and device settings were not changed.

| CPU activation reference | Mean absolute score difference | Maximum absolute score difference | Exact score matches |
|---|---:|---:|---:|
| Earlier generic LUT257, including known output half-logit boundary | 2.2216023e-5 | 2.8427029e-4 | 0/257 |
| Public table, real-Q interpolation | 4.2948130e-5 | 8.5535773e-5 | 0/257 |
| Public table, floor-Q interpolation | 4.0705105e-5 | 8.6351979e-5 | 0/257 |
| Public table, nearest-even-Q interpolation | 4.2333090e-5 | 8.5595236e-5 | 0/257 |

All three full-NN public-table rows include the explicit outside-range saturation hypothesis. No best candidate was subsequently fitted or substituted into the deployed detector.

The local ensemble tensor's maximum difference fell from `0.00366211` to `0.00048828125`. The local output tensor's maximum difference instead increased from `0.000770569` to `0.00128173828125`.

Domain coverage limits the interpretation:

- Ensemble visible inputs outside the public range: **5,545/25,700 (21.576%)**.
- Output visible inputs outside the public range: **2,844/6,682 (42.562%)**.
- Rows whose entire pipeline could be evaluated without an outside-range hypothesis: **0/257**.

For the floor-Q candidate, after excluding out-of-domain values and their hidden dependencies, local exact matches were **13,320/20,113 eligible ensemble components** and **3,084/3,838 eligible output components**. These component matches are not full-tensor or final-score matches.

The same 257 records had already been inspected. The first 160 and remaining 97 records were retained as descriptive partitions; neither part is an untouched holdout. There are no attack labels or intrusion-detection accuracy claims here.

## What static RKNN inspection could not establish

Four saved ensemble/output exports at optimization levels 0 and 3 were inspected without initializing the SDK. Their RKNN-v6 outer envelopes and trailing metadata were readable, but the metadata exposed a single NNBG graph and its ports, not decoded LUT contents or the necessary register settings. Saved compiler records showed fusion/type/shape information, but did not identify index scale, start/end, slope or output conversion settings.

A literal search in the two saved optimization-0 models found no contiguous occurrence of the public full table or either half-table. Unknown packing, encoding and command layout mean this **does not prove that the actual LUT differs**. Encryption, compression or runtime generation were not established either. No verified decoder for the relevant body layout was found within the inspected materials.

The next numerical question is therefore the actual RKNN activation/conversion configuration and fused precision boundaries. Calling the current CPU function a hardware emulator would overstate the evidence.

## Reproduction and evidence limits

The accompanying CPU-only reference exposes the table construction, domain check and three interpolation hypotheses. Its table hash can be checked using public inputs alone. It does not load a model, access the network, initialize an SDK or run on the router. Python is an offline analysis dependency, not a new R6S dependency.

The household model, weights, input features and per-record NPU traces are private and are not distributed. Consequently, the public code reproduces the table/hypothesis calculation, **not the full private 257-record NN experiment**. Aggregated results, pinned sources, fixed conditions and limitations are published; the original inputs, results, manifests and provenance are retained privately.

One comparison runner caught a successful `SystemExit(0)` and wrote an erroneous failure marker after the numeric result and manifest had already been saved. This was an exit-recording defect, not a failed NPU calculation. The original marker and results were preserved, a separate correction was recorded, and the comparison was not silently rerun or overwritten.

No fresh NPU/SDK run, training, router configuration change, restart, automatic blocking or additional alerting was performed for this CPU investigation. Exact numerical reproduction remains unmet. Detection accuracy, unseen traffic behavior and long-duration stability require separate experiments.
