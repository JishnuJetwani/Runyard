# Kubernetes execution

Set `RUNYARD_EXECUTION_MODE=kubernetes`. The coordinator uses its projected service
account token (re-read on each API request) and cluster CA to create and reconcile
Jobs in `RUNYARD_KUBERNETES_NAMESPACE`. Docker agent RPCs are disabled in this mode.

Jobs enter in priority order, then submission order. `RUNYARD_MAX_ACTIVE_JOBS`
limits admission (default 16; the supplied deployment uses 8), including attempts
with pending cleanup. Kubernetes places Pods using their resource requests and
limits. Attempt history shows `@kubernetes` instead of a Docker worker ID.

Attempts are saved before calling the Kubernetes API. Deterministic Job names
allow the coordinator to check a lost create response. Jobs use one completion,
`restartPolicy: Never`, and `backoffLimit: 0`, leaving retries to Runyard. Every
runner must claim ownership because Kubernetes can start a Job more than once.

Cleanup stays pending until the Job and its Pods are gone. Missing or terminal
Jobs trigger recovery without waiting for the launch deadline. For failures, the
adapter waits for `Failed`, not just `FailureTarget`. Expired execution deadlines
follow the timeout retry policy. Job observations cannot overwrite a result already
committed by the runner. Launch and lease deadlines still apply during API outages.

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

Start `scripts/kind-forward.sh` in another terminal and set
`RUNYARD_URL=http://127.0.0.1:8081` plus `RUNYARD_PROFILE=development` for the
native CLI. The helper reconnects after a server Pod replacement; ordinary
`kubectl port-forward` exits when its selected Pod disappears. Publish the fixture
with `scripts/publish-fixture.sh`; the cluster registry alias resolves its same
`localhost:5001/...@sha256:...` digest.
