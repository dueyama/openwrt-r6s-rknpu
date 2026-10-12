# SPDX-License-Identifier: GPL-3.0-or-later
"""Public CPU reference checks; no private model, SDK, or device access."""
import hashlib
import unittest

import numpy as np

import rk3588_sigmoid_reference as ref


class ReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        half = np.arange(65536, dtype=np.uint16).view(np.float16)
        cls.finite = np.sort(half[np.isfinite(half)].astype(np.float32))

    def test_pinned_table_bytes(self):
        raw = np.concatenate((ref.LE, ref.LO)).astype('<u2').tobytes()
        self.assertEqual(len(raw), 2052)
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '47363f344838cb18c0f36f4d320e6af4d6ca0eb7bb8476141be4b0700d36d422')
        self.assertEqual(ref.TABLE_SHA256, hashlib.sha256(raw).hexdigest())
        self.assertEqual(ref.LE.shape, (513,))
        self.assertEqual(ref.LO.shape, (513,))
        self.assertEqual(int(ref.LE[-1]), 16384)
        self.assertEqual(int(ref.LO[0]), 16384)

    def test_tables_are_read_only(self):
        for table in (ref.LE, ref.LO):
            self.assertFalse(table.flags.writeable)
            with self.assertRaises(ValueError):
                table[0] = 0

    def test_domain_over_all_finite_half_patterns(self):
        # Independent domain comparison in FP64 over all 63,488 finite patterns.
        expected = np.abs(self.finite.astype(np.float64)) <= 16384.0 / 2596.0
        np.testing.assert_array_equal(ref.supported_input_mask(self.finite), expected)

    def test_default_rejects_outside_domain_with_counts(self):
        with self.assertRaises(ref.OutsidePublishedDomain) as caught:
            ref.activation(np.array([-10.0, 0.0, 10.0], dtype=np.float32))
        self.assertEqual((caught.exception.count, caught.exception.total), (2, 3))

    def test_clamp_all_finite_half_patterns(self):
        for profile in ref.PROFILES:
            with self.subTest(profile=profile):
                result = ref.activation(self.finite, profile, 'endpoint_clamp')
                self.assertEqual(result.dtype, np.dtype(np.float16))
                self.assertTrue(np.all(np.isfinite(result)))
                self.assertTrue(np.all(np.diff(result.astype(np.float32)) >= 0))
                self.assertTrue(np.all((result >= 0) & (result <= 1)))
                endpoints = np.array([(int(ref.LE[0]) + 1) / 32768.0,
                                      (int(ref.LO[-1]) + 1) / 32768.0], dtype=np.float16)
                np.testing.assert_array_equal(result[[0, -1]], endpoints)

    def test_rounding_hypotheses_remain_distinct(self):
        values = self.finite[ref.supported_input_mask(self.finite)]
        outputs = [ref.activation(values, p).view(np.uint16) for p in ref.PROFILES]
        for i in range(len(outputs)):
            for j in range(i + 1, len(outputs)):
                self.assertTrue(np.any(outputs[i] != outputs[j]))

    def test_shape_scalar_empty_and_signed_zero(self):
        for shape in [(), (0,), (2, 3)]:
            result = ref.activation(np.zeros(shape, dtype=np.float32))
            self.assertEqual(result.shape, shape)
            self.assertEqual(result.dtype, np.dtype(np.float16))
        result = ref.activation(np.array([-0.0, 0.0], dtype=np.float32))
        np.testing.assert_array_equal(result, np.array([0.5, 0.5], dtype=np.float16))

    def test_invalid_numeric_inputs(self):
        for value in [np.nan, np.inf, -np.inf, np.finfo(np.float32).max]:
            for operation in (ref.activation, ref.supported_input_mask):
                with self.subTest(value=value, operation=operation.__name__):
                    with self.assertRaises(ValueError):
                        operation(np.array([value], dtype=np.float32))

    def test_invalid_policy_names(self):
        with self.assertRaises(ValueError):
            ref.activation([0.0], profile='unknown')
        with self.assertRaises(ValueError):
            ref.activation([0.0], outside='unknown')


if __name__ == '__main__':
    unittest.main()
