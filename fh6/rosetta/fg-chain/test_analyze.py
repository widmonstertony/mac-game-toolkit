import json
import unittest
from analyze import analyze


def records():
    tags = [dict(type=i, ptr=f'{i+10:x}', size=[128, 96], extent=[0, 0, 128, 96]) for i in range(6)]
    native = dict(frame=60, constants=1, jitter=[0.25, -0.125], scale=[0.5, -0.5], flags=[1, 1, 0, 0, 0, 0], tags=tags)
    rows = [dict(event='observer_ready', pid=1, tick=2, abi=1)]
    rows += [dict(event='capture_export', name=n) for n in ['slSetConstants', 'slSetTagForFrame', 'slEvaluateFeature']]
    rows += [dict(event='prepare', contextFrame=60, native=native, jitter=native['jitter'], scale=[64, -48],
                  depth=dict(ptr=tags[0]['ptr']), motion=dict(ptr=tags[1]['ptr']))]
    return rows


def run(rows):
    return analyze(json.dumps(r) for r in rows)


class AnalyzeTest(unittest.TestCase):
    def test_matching_frame_is_not_claimed_as_visual_success(self):
        result = run(records())
        self.assertTrue(result['coverage_complete'])
        self.assertEqual(result['finding_count'], 0)
        self.assertIn('do not prove', result['limits'])

    def test_signed_motion_scale_difference(self):
        rows = records(); rows[-1]['scale'] = [64, 48]
        self.assertEqual(run(rows)['findings'][0]['issue'], 'motion_scale_mismatch')

    def test_pointer_difference_is_not_invented_staleness(self):
        rows = records(); rows[-1]['depth']['ptr'] = 'abcdef'
        result = run(rows)
        self.assertEqual(result['finding_count'], 0)
        self.assertEqual(result['counts']['depth_different_resource_needs_copy_provenance'], 1)

    def test_missing_capture_is_incomplete(self):
        rows = records(); rows[-1]['native'] = None
        self.assertFalse(run(rows)['coverage_complete'])

    def test_process_restart_does_not_mix_reused_frame_ids(self):
        rows = records()
        rows += [dict(event='observer_ready', pid=2, tick=9, abi=1)]
        result = run(rows)
        self.assertFalse(result['coverage_complete'])
        self.assertEqual(result['session']['pid'], 2)
        self.assertEqual(result['finding_count'], 0)


if __name__ == '__main__':
    unittest.main()
