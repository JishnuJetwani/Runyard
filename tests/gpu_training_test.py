"""Check workload math and file outputs independently of the execution backend."""
import importlib.util
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / "examples/gpu-training/train.py"
spec = importlib.util.spec_from_file_location("gpu_training", source)
training = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = training
spec.loader.exec_module(training)


class Training(unittest.TestCase):
    def test_parameters_are_bounded(self):
        for values in ({"epochs": 0}, {"seed": True}, {"batch_size": 1}, {"learning_rate": float("nan")}, {"unknown": 1}):
            with self.assertRaises(ValueError):
                training.Parameters.parse(values)

    def test_training_emits_metrics_and_loadable_outputs(self):
        training.torch.set_num_threads(1)
        parameters = training.Parameters(epochs=2)
        records = []
        model, results = training.train(parameters, training.torch.device("cpu"), lambda *args: records.append(args))
        self.assertEqual(len(records), 4)
        self.assertTrue(all(math.isfinite(record[2]) for record in records))
        self.assertGreater(results["accuracy"], 0.8)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            training.save_outputs(model, parameters, results, {"type": "cpu"}, output)
            checkpoint = training.torch.load(output / "model.pt", weights_only=True)
            self.assertIn("state_dict", checkpoint)
            self.assertTrue((output / "summary.json").is_file())

    def test_entrypoint_requires_cuda(self):
        with tempfile.TemporaryDirectory() as directory:
            parameters = Path(directory) / "parameters.json"
            parameters.write_text("{}")
            with patch.dict(training.os.environ, {"RUNYARD_PARAMETERS_PATH": str(parameters)}), patch.object(training.torch.cuda, "is_available", return_value=False):
                with self.assertRaisesRegex(RuntimeError, "requires CUDA"):
                    training.main()


if __name__ == "__main__":
    unittest.main()
