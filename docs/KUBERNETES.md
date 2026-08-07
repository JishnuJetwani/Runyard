# Kubernetes execution

Set `RUNYARD_EXECUTION_MODE=kubernetes`. The coordinator uses its projected service
account token (re-read on each API request) and cluster CA to create and reconcile
Jobs in `RUNYARD_KUBERNETES_NAMESPACE`. Docker agent RPCs are disabled in this mode.

Admission is priority/FIFO and limited by `RUNYARD_MAX_ACTIVE_JOBS` (default 16;
deployment defaults to 8). Kubernetes places Pods using each experiment's resource
requests/limits. Admission counts include pending cleanup. `@kubernetes` in attempt
history identifies this backend; it is not a registered Docker worker.

Attempts are saved before calling Kubernetes. Deterministic Job names let the
coordinator check a lost create response. Jobs use one completion,
`restartPolicy: Never`, and `backoffLimit: 0`, leaving retries to Runyard.
Every runner must claim ownership because Kubernetes can start a Job more than
once. Cleanup stays pending until the Job and its Pods are gone. Missing or failed
runtimes recover through the same launch and lease deadlines as Docker.

Workload Pods use a service account without role bindings, disable projected API
credentials, and receive only their attempt capability. The coordinator role can
create/get/list/delete Jobs and get/list Pods in its namespace.

For local setup, build the images, then run `scripts/kind-setup.sh`. This creates
only cluster `runyard` and uses context `kind-runyard` explicitly. It installs a
private PostgreSQL StatefulSet, persistent filesystem artifacts, and the coordinator.
The local overlay explicitly enables plaintext development transport. Cloud/base
configuration instead requires TLS and a `runyard-tls` Secret plus `runyard-ca`
ConfigMap containing `ca.crt` for experiment Pods.

The local registry alias follows the [kind registry guide](https://kind.sigs.k8s.io/docs/user/local-registry/).
Jobs retain the common [runner ownership contract](https://kubernetes.io/docs/concepts/workloads/controllers/job/).
No live AWS cluster has been provisioned or tested.
