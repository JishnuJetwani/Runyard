import importlib.util
import os
import pathlib
import tempfile
import unittest

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_source_times", root / "scripts/build-source-times.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SourceTimes(unittest.TestCase):
    def test_changed_content_advances_time_and_replayed_content_keeps_it(self):
        with tempfile.TemporaryDirectory() as directory:
            source, cache = pathlib.Path(directory) / "source", pathlib.Path(directory) / "cache"
            source.mkdir()
            path = source / "example.cpp"
            path.write_text("first")
            module.synchronize(source, cache)
            original = path.stat().st_mtime_ns
            os.utime(path, ns=(1, 1))
            module.synchronize(source, cache)
            self.assertEqual(path.stat().st_mtime_ns, original)
            path.write_text("changed")
            os.utime(path, ns=(1, 1))
            module.synchronize(source, cache)
            updated = path.stat().st_mtime_ns
            self.assertGreater(updated, original)
            os.utime(path, ns=(1, 1))
            module.synchronize(source, cache)
            self.assertEqual(path.stat().st_mtime_ns, updated)


if __name__ == "__main__":
    unittest.main()
