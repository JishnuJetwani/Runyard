import datetime
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'benchmarks'))
from run import timestamp


class TimestampParsing(unittest.TestCase):
    def test_postgresql_offsets_and_variable_fraction(self):
        expected = datetime.datetime(2026, 9, 18, 7, 42, 21, 600000,
                                     tzinfo=datetime.timezone.utc).timestamp()
        for value in ('2026-09-18 07:42:21.6+00', '2026-09-18T07:42:21.600000Z',
                      '2026-09-18 09:12:21.6+01:30'):
            self.assertEqual(timestamp(value), expected)

    def test_whole_seconds(self):
        self.assertEqual(timestamp('1970-01-01 00:00:00+00'), 0)


if __name__ == '__main__':
    unittest.main()
