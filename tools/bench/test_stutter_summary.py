"""Regression checks for attribution of worker work and repeated pictures."""
import csv
import tempfile
import unittest
from pathlib import Path
from stutter_summary import summarize


class StutterSummaryTest(unittest.TestCase):
    def summary(self, rows, **kwargs):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'events.csv'
            with trace.open('w', newline='') as target:
                writer = csv.writer(target)
                writer.writerow(('kind', 'frame', 'key', 'start_ns', 'end_ns', 'thread', 'result'))
                writer.writerows(rows)
            return summarize(trace, **kwargs)

    def test_repeats_preserve_guest_stall_and_requests_are_not_compiles(self):
        result = self.summary([
            ('frame', 10, '0', 100000000, 200000000, 1, 0),
            ('pipeline_request', 10, 'abc', 110000000, 110000000, 1, 1),
            ('guest_pipeline', 10, 'abc', 111000000, 180000000, 2, 0),
            ('pipeline_pending', 10, 'abc', 110000000, 190000000, 1, 0),
            ('repeated_frame', 10, '0', 150000000, 160000000, 1, 0),
            ('repeated_frame_age', 10, '0', 100000000, 160000000, 1, 0),
            ('startup_pipeline_hit', 10, 'def', 195000000, 195000000, 1, 0),
            ('shader_request', 10, 'a', 110000000, 110000000, 1, 1),
            ('shader_pending', 10, 'a', 110000000, 115000000, 1, 0),
            ('spirv_compile', 10, 'a', 111000000, 114000000, 3, 0),
        ])
        self.assertEqual(result['max_ms'], 100)
        self.assertEqual(result['pipeline_creation_calls'], 1)
        self.assertEqual(result['render_thread_pipeline_creation_calls'], 0)
        self.assertEqual(result['other_thread_pipeline_creation_calls'], 1)
        self.assertEqual(result['demand_requests'], 1)
        self.assertEqual(result['shader_requests'], 1)
        self.assertEqual(result['shader_pending_execution']['total_ms'], 5)
        self.assertEqual(result['render_thread_spirv_compiles'], 0)
        self.assertEqual(result['other_thread_spirv_compiles'], 1)
        self.assertEqual(result['startup_pipeline_hits'], 1)
        self.assertEqual(result['pending_execution']['total_ms'], 80)
        self.assertEqual(result['repeated_presentations'], 1)
        self.assertEqual(result['max_repeated_picture_age_ms'], 60)
        self.assertEqual(result['worst_frames'][0]['render_thread_compilations'], 0)

    def test_worker_attribution_uses_execution_time_after_frame_reset(self):
        result = self.summary([
            ('frame', 10, '0', 100000000, 200000000, 1, 0),
            ('frame', 2, '0', 200000000, 300000000, 1, 0),
            ('startup_pipeline', 0, 'a', 110000000, 120000000, 2, 0),
            ('startup_pipeline', 10, 'b', 210000000, 220000000, 2, 0),
        ], frame_from=10, frame_to=11)
        self.assertEqual(result['frame_count'], 1)
        self.assertEqual(result['pipeline_creation_calls'], 1)
        self.assertEqual(result['worst_frames'][0]['pipeline_compilations'][0]['key'], 'a')


if __name__ == '__main__':
    unittest.main()
