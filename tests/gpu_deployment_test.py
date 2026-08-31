"""Check GPU deployment contracts in rendered configuration."""
import json
import os
import subprocess
import yaml


def render(directory):
    return list(yaml.safe_load_all(subprocess.check_output(["kubectl", "kustomize", directory], text=True)))


nvidia = render("deploy/kubernetes/nvidia")
plugin = next(item for item in nvidia if item["kind"] == "DaemonSet")
pod = plugin["spec"]["template"]["spec"]
assert pod["runtimeClassName"] == "nvidia"
assert pod["nodeSelector"] == {"runyard.io/gpu-mode": "exclusive"}
assert pod["automountServiceAccountToken"] is False
container = pod["containers"][0]
assert "@sha256:" in container["image"]
assert "--mig-strategy=none" in container["args"]
assert "--device-id-strategy=uuid" in container["args"]
assert not any("sharing" in arg for arg in container["args"])
application = render("deploy/kubernetes/overlays/gpu")
config = next(item for item in application if item["kind"] == "ConfigMap")
assert config["data"]["RUNYARD_KUBERNETES_GPU_RUNTIME_CLASS"] == "nvidia"
role = next(item for item in application if item["kind"] == "ClusterRole")
assert role["rules"] == [{"apiGroups": [""], "resources": ["nodes", "pods"], "verbs": ["get", "list"]}]
workload = next(item for item in application if item["kind"] == "ServiceAccount" and item["metadata"]["name"] == "workload")
assert workload["automountServiceAccountToken"] is False

environment = dict(os.environ, RUNYARD_GPU_UUIDS="GPU-fixture", RUNYARD_OWNER_TOKEN="owner",
                   RUNYARD_WORKER_TOKEN="worker", RUNYARD_SIGNING_KEY="signing")
compose = json.loads(subprocess.check_output(["docker", "compose", "-f", "compose.yaml", "-f", "deploy/gpu.compose.yaml",
                                              "--profile", "gpu", "config", "--format", "json"], env=environment, text=True))
agent = compose["services"]["agent-gpu"]
assert agent["environment"]["RUNYARD_GPU_MODE"] == "nvidia"
assert agent["environment"]["RUNYARD_GPU_UUIDS"] == "GPU-fixture"
assert agent["environment"]["RUNYARD_WORKER_TOKEN"] == "worker"
assert agent["deploy"]["resources"]["reservations"]["devices"][0]["driver"] == "nvidia"
print("GPU deployment rendering passed")
