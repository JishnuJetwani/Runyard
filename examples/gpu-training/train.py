"""A small CUDA training workload using Runyard's file-based integration contract."""
from dataclasses import asdict, dataclass
import json
import math
import os
from pathlib import Path

os.environ.setdefault("CUBLAS_WORKSPACE_CONFIG", ":4096:8")
import torch
from torch import nn


@dataclass(frozen=True)
class Parameters:
    learning_rate: float = 0.01
    batch_size: int = 128
    epochs: int = 10
    seed: int = 7

    @classmethod
    def parse(cls, values):
        unknown = set(values) - set(cls.__dataclass_fields__)
        if unknown:
            raise ValueError(f"Unknown parameters: {sorted(unknown)}")
        result = cls(**values)
        for name, low, high in (("batch_size", 8, 8192), ("epochs", 1, 1000), ("seed", 0, 2**31 - 1)):
            value = getattr(result, name)
            if type(value) is not int or not low <= value <= high:
                raise ValueError(f"{name} must be an integer in [{low}, {high}]")
        if type(result.learning_rate) not in (int, float) or not math.isfinite(result.learning_rate) or not 0 < result.learning_rate <= 1:
            raise ValueError("learning_rate must be finite and in (0, 1]")
        return result


def train(parameters, device, emit):
    torch.manual_seed(parameters.seed)
    generator = torch.Generator().manual_seed(parameters.seed)
    inputs = torch.randn(4096, 16, generator=generator).to(device)
    labels = (inputs[:, :4].sum(dim=1) > 0).long()
    model = nn.Sequential(nn.Linear(16, 64), nn.ReLU(), nn.Linear(64, 2)).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=parameters.learning_rate)
    loss_function = nn.CrossEntropyLoss()
    for epoch in range(parameters.epochs):
        model.train()
        order = torch.randperm(len(inputs), generator=generator).to(device)
        for indices in order.split(parameters.batch_size):
            optimizer.zero_grad(set_to_none=True)
            loss = loss_function(model(inputs[indices]), labels[indices])
            loss.backward()
            optimizer.step()
        model.eval()
        with torch.no_grad():
            prediction = model(inputs)
            loss_value = loss_function(prediction, labels).item()
            accuracy = (prediction.argmax(dim=1) == labels).float().mean().item()
        if not math.isfinite(loss_value):
            raise RuntimeError("Training produced a nonfinite loss")
        emit("loss", epoch, loss_value)
        emit("accuracy", epoch, accuracy)
        print(f"epoch={epoch + 1} loss={loss_value:.5f} accuracy={accuracy:.4f}", flush=True)
    return model, {"loss": loss_value, "accuracy": accuracy}


def save_outputs(model, parameters, results, device_info, output):
    output.mkdir(parents=True, exist_ok=True)
    torch.save({"state_dict": model.cpu().state_dict(), "parameters": asdict(parameters)}, output / "model.pt")
    summary = {"parameters": asdict(parameters), "results": results, "device": device_info,
               "torch_version": torch.__version__, "cuda_version": torch.version.cuda}
    (output / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n")


def main():
    parameters = Parameters.parse(json.loads(Path(os.environ["RUNYARD_PARAMETERS_PATH"]).read_text()))
    if not torch.cuda.is_available():
        raise RuntimeError("This workload requires CUDA and a Runyard GPU allocation")
    count = torch.cuda.device_count()
    if count != 1:
        raise RuntimeError(f"This one-GPU workload expected 1 visible device, found {count}")
    torch.set_num_threads(4)
    torch.use_deterministic_algorithms(True)
    properties = torch.cuda.get_device_properties(0)
    device_info = {"type": "cuda", "name": properties.name, "visible_count": count,
                   "memory_bytes": properties.total_memory,
                   "nvidia_visible_devices": os.environ.get("NVIDIA_VISIBLE_DEVICES", "")}
    print(json.dumps(device_info), flush=True)
    with Path(os.environ["RUNYARD_METRICS_PATH"]).open("a", buffering=1) as metrics:
        def emit(name, step, value):
            metrics.write(json.dumps({"name": name, "step": step, "value": value}, allow_nan=False) + "\n")
        model, results = train(parameters, torch.device("cuda:0"), emit)
    save_outputs(model, parameters, results, device_info, Path(os.environ["RUNYARD_OUTPUT_DIR"]))


if __name__ == "__main__":
    main()
