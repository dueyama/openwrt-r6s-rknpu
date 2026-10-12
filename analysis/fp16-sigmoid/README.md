# CPU reference accompanying the FP16 investigation

Read [the numerical investigation](FP16_SIGMOID_LUT.md) before using this reference.

`rk3588_sigmoid_reference.py` is an offline mathematical reference, not a driver, an RKNN emulator, or a detector. It needs Python and NumPy on an analysis machine. It adds no Python or other dependency to the R6S runtime.

Use an existing analysis environment with Python 3 and NumPy, or install NumPy in
a separate virtual environment. This directory is not imported or packaged by
the driver, OpenWrt package or runtime build recipes.

From this directory, the public table-only check is:

```sh
python3 -c 'import rk3588_sigmoid_reference as r; print(r.TABLE_SHA256)'
```

Expected SHA-256:

```text
47363f344838cb18c0f36f4d320e6af4d6ca0eb7bb8476141be4b0700d36d422
```

`activation(x, profile="linear_real_q", outside="error")` takes finite FP32 values and returns NumPy binary16 results. The default refuses out-of-domain values. `outside="endpoint_clamp"` is an explicitly unverified saturation hypothesis. The other profiles are `linear_floor_q` and `linear_rne_q`.

Run the public CPU checks from this directory with:

```sh
python3 -B test_reference.py
```

These nine checks cover the pinned table hash, domain handling, shape and invalid
inputs, and the three hypotheses over all 63,488 finite binary16 bit patterns.
They do not compare against private NN records or certify hardware behavior.
The [dated verification record](VERIFICATION.json) identifies the publication
checks separately from the reported private comparison.

The reference credits the pinned rocket-userspace source in its header. The
module and its CPU tests are separately distributed under GPL-3.0-or-later;
[COPYING](COPYING) contains the unmodified GPL version 3 text. Documentation and
provenance retain the repository's GPL-2.0-only default. This independent analysis
module must not be linked into the kernel driver. Exact source pins, component
licenses and the original/publication hashes are in [PROVENANCE.json](PROVENANCE.json)
and the repository's [license inventory](../../LICENSE). NumPy is an external
dependency with its own license notices and is not bundled here.

No private model, weights, household inputs, trace or deployment configuration is included. The full 257-record experiment cannot be reproduced from this package alone.
