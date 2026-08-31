# Small GPU training experiment

This workload trains a two-layer classifier on a seeded synthetic dataset. It
requests one NVIDIA GPU, writes loss/accuracy metrics each epoch, and publishes
`model.pt` and `summary.json` through the standard runner contract.

Build the runner base for Linux/amd64 using the main Dockerfile, push it to your
registry, then build this workload from its immutable digest:

```sh
docker build --platform linux/amd64 \
  --build-arg RUNYARD_RUNNER_BASE="$RUNNER_BASE_DIGEST" \
  -t "$GPU_TRAINING_IMAGE" examples/gpu-training
```

The supplied base uses Ubuntu 24.04, keeping the runner and workload on the same
C library baseline. The Python 3.12 lock selects PyTorch 2.8.0 with CUDA 12.8 and
pins all dependencies by version and hash. Install a host driver compatible with
that CUDA runtime; the workload does not need a compiler or CUDA toolkit on the
host. See [PyTorch distributions](https://pytorch.org/get-started/previous-versions/#v2-8-0)
and [NVIDIA driver compatibility](https://docs.nvidia.com/deploy/cuda-compatibility/minor-version-compatibility.html).

Push the image, replace the placeholder digest in `spec.json` with its published
reference, and submit it with `runyard submit`. Use the existing sweep format to
vary learning rate and seed. `epochs`, `batch_size`, `learning_rate`, and `seed` are
the accepted parameters. The entrypoint requires CUDA and exactly one visible GPU.
Metrics describe the synthetic training set; they are not a model-quality benchmark.

To regenerate the Linux dependency lock:

```sh
uv pip compile examples/gpu-training/requirements.in --python-version 3.12 \
  --python-platform x86_64-manylinux_2_28 --index-url https://download.pytorch.org/whl/cu128 \
  --generate-hashes --emit-index-url --output-file examples/gpu-training/requirements.lock
```
