# SPDX-License-Identifier: GPL-3.0-or-later
# CPU analysis adaptation, 2026-10-12, openwrt-r6s-rknpu contributors.
# Table-generation algorithm reference: Greg Ordinary and rocket-userspace contributors.
# https://github.com/gregordinary/rocket-userspace/blob/1401c46e75e45adee66b08b97a62e633306de016/src/rocket_activation.c
# Referenced source copyright: (C) 2026 The rocket-userspace authors.
# Referenced source declares GPL-3.0-or-later. No driver/SDK code is included.
# No warranty. See the separately supplied GPL-3.0 license text.
"""Independent CPU hypotheses for a published RK3588 standalone sigmoid LUT.

This module executes no driver, SDK, or device code. A pure C table-builder
excerpt was tested separately. This is mathematical analysis, not a verified
RKNN/rocket/NPU emulator or a hardware specification.
Q interpolation intermediate rounding and outside LE slope semantics remain unknown.
"""
import hashlib
import math
import numpy as np

ROCKET_COMMIT = '1401c46e75e45adee66b08b97a62e633306de016'
NOTES_COMMIT = 'b4efde2a0a083bbed2f074ffdf3343bcd8a92cb3'
PROFILES = ('linear_real_q', 'linear_floor_q', 'linear_rne_q')
OUTSIDE_POLICIES = ('error', 'endpoint_clamp')
INPUT_SCALE = 2596.0
INDEX_LIMIT = 16384.0
INDEX_STEP = 32.0
INPUT_SUPPORT_MIN = -INDEX_LIMIT / INPUT_SCALE
INPUT_SUPPORT_MAX = INDEX_LIMIT / INPUT_SCALE

class OutsidePublishedDomain(ValueError):
    def __init__(self, count, total):
        self.count = int(count)
        self.total = int(total)
        super().__init__(f'{self.count}/{self.total} FP16-rounded inputs outside published LUT domain; LE slope decode unconfirmed')

def _lround_away(x):
    # C lround: nearest integer, half away from zero, independent of fenv mode.
    # All sigmoid Q inputs here are positive and far from exact half-integer ties.
    return math.floor(x + 0.5) if x >= 0.0 else -math.floor(-x + 0.5)

def build_tables():
    # Mirrors the fixed author's build_lut_unit evaluation order and endpoints.
    step = 32.0 / 2596.0
    le, lo = [], []
    for i in range(513):
        x = -float(512-i) * step
        y = 1.0 / (1.0 + math.exp(-x))
        le.append(min(32767, max(0, _lround_away(y * 32768.0))))
    for i in range(513):
        x = float(i) * step
        y = 1.0 / (1.0 + math.exp(-x))
        lo.append(min(32767, max(0, _lround_away(y * 32768.0))))
    return np.asarray(le, dtype=np.uint16), np.asarray(lo, dtype=np.uint16)

LE, LO = build_tables()
LE.flags.writeable = False
LO.flags.writeable = False
TABLE_SHA256 = hashlib.sha256(np.concatenate((LE, LO)).astype('<u2').tobytes()).hexdigest()

def _indices(x):
    # Caller contract is FP32 visible logits. Device standalone input is FP16,
    # read widened to FP32 (MRDMA_FP16TOFP32). Product precision here is an
    # explicitly assumed FP32 CPU operation; actual DPU BN arithmetic is unknown.
    values = np.asarray(x, dtype=np.float32)
    if not np.all(np.isfinite(values)):
        raise ValueError('nonfinite input')
    with np.errstate(over='ignore', invalid='ignore'):
        half = values.astype(np.float16)
    if not np.all(np.isfinite(half)):
        raise ValueError('input overflows finite FP16')
    index = np.multiply(half.astype(np.float32), np.float32(INPUT_SCALE), dtype=np.float32)
    return index

def supported_input_mask(x):
    index = _indices(x)
    return (index >= np.float32(-INDEX_LIMIT)) & (index <= np.float32(INDEX_LIMIT))

def activation(x, profile='linear_real_q', outside='error'):
    """FP32 logits ndarray -> FP16 results, shape preserved; no model/data access.

    Profiles apply three pre-frozen Q interpolation rounding hypotheses. Outside
    'error' refuses unconfirmed slope reproduction. 'endpoint_clamp' is one separate
    CPU saturation hypothesis, NOT the published LE slope/register interpretation.
    """
    if profile not in PROFILES:
        raise ValueError('unknown interpolation profile')
    if outside not in OUTSIDE_POLICIES:
        raise ValueError('unknown outside policy')
    index = _indices(x)
    support = (index >= np.float32(-INDEX_LIMIT)) & (index <= np.float32(INDEX_LIMIT))
    if outside == 'error' and not np.all(support):
        raise OutsidePublishedDomain(np.count_nonzero(~support), support.size)
    # Optional endpoint clamp is visibly separate from the public core.
    z = np.clip(index.astype(np.float64), -INDEX_LIMIT, INDEX_LIMIT)
    # Negative -> LE table on [-16384,0], nonnegative (including signed zero) -> LO.
    neg = z < 0.0
    pos = np.where(neg, (z + INDEX_LIMIT) / INDEX_STEP, z / INDEX_STEP)
    lower = np.minimum(np.floor(pos).astype(np.int64), 511)
    fraction = pos - lower.astype(np.float64)
    tables = np.stack((LE, LO)).astype(np.float64)
    table_id = np.where(neg, 0, 1)
    q0, q1 = tables[table_id, lower], tables[table_id, lower+1]
    # Scalar linear interpolation modeled in FP64 for transparent exact formula;
    # no claim about hardware fraction bit width, FP or integer evaluation order.
    q = q0 + fraction * (q1-q0)
    if profile == 'linear_floor_q':
        q = np.floor(q)
    elif profile == 'linear_rne_q':
        q = np.rint(q)
    # Author's OUT_CVT affine reading: offset +1 Q LSB, scale 1, minus_exp=15.
    # Final NumPy binary16 cast is nearest-even, not a device rounding proof.
    y = np.multiply(q + 1.0, 2.0**-15)
    result = y.astype(np.float16)
    if not np.all(np.isfinite(result)):
        raise ValueError('nonfinite result')
    return result
