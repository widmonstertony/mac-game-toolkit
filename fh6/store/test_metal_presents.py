from fractions import Fraction
import unittest

from metal_presents import display_timeline


class DisplayTimelineTest(unittest.TestCase):
    def test_inverse_mach_units_preserve_display_stall(self):
        timebase = Fraction(125, 3)
        ns = [0, 8_333_333, 16_666_666, 1_125_000_000]
        callbacks = [(1_000_000_000 + t, 9_000_000_000 + round(t * timebase)) for t in ns]
        callbacks.append((callbacks[-1][0] + 12_000, callbacks[-1][1]))
        self.assertEqual(display_timeline(callbacks, timebase), (ns, "inverse-mach-timebase"))
        self.assertEqual(display_timeline(callbacks, None), ([], None))

    def test_nanoseconds_do_not_require_timebase(self):
        ns = [0, 8_333_333, 16_666_666]
        callbacks = [(100_000_000 + t, 9_000_000_000 + t) for t in ns]
        self.assertEqual(display_timeline(callbacks, None), (ns, "nanoseconds"))

    def test_matching_span_alone_does_not_establish_a_clock(self):
        callbacks = [(1, 1), (2_000_000, 32_000_000), (50_000_001, 50_000_001)]
        self.assertEqual(display_timeline(callbacks, Fraction(125, 3)), ([], None))


if __name__ == "__main__":
    unittest.main()
