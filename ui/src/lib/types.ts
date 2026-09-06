export type Scalar = string | number | boolean | null;
export type Status =
  | 'QUEUED'
  | 'STARTING'
  | 'RUNNING'
  | 'FINALIZING'
  | 'RETRY_WAIT'
  | 'SUCCEEDED'
  | 'FAILED'
  | 'CANCELLED';
export const statuses: Status[] = [
  'QUEUED',
  'STARTING',
  'RUNNING',
  'FINALIZING',
  'RETRY_WAIT',
  'SUCCEEDED',
  'FAILED',
  'CANCELLED',
];
export const terminal = (status: Status) => ['SUCCEEDED', 'FAILED', 'CANCELLED'].includes(status);
export interface Resources {
  cpu_millis: number;
  memory_mib: number;
  gpu_count?: number;
}
export interface RunSpec {
  version: 1;
  name: string;
  image: string;
  command: string[];
  parameters: Record<string, Scalar>;
  environment: Record<string, string>;
  labels: Record<string, string>;
  resources: Resources;
  timeout_seconds: number;
  priority: number;
  retry: { max_attempts: number; retry_exit: boolean; retry_timeout: boolean };
  source: { repository: string; revision: string };
}
export interface Run {
  id: string;
  spec: RunSpec;
  status: Status;
  generation: number;
  active_attempt: string;
  sweep_id: string;
  parent_run_id: string;
  created_at: string;
  updated_at: string;
}
export interface Device {
  uuid: string;
  name: string;
  memory_mib: number;
  eligible: boolean;
  reason: string;
}
export interface Allocation {
  device: Device;
  allocated_at: string;
  released_at: string;
}
export interface Attempt {
  id: string;
  run_id: string;
  generation: number;
  worker_id: string;
  status: Status;
  reason: string;
  runtime_id: string;
  cleanup_status: 'PENDING' | 'DONE';
  created_at: string;
  started_at: string;
  finished_at: string;
  acknowledged_sequence: number;
  gpu_count: number;
  gpu_allocations: Allocation[];
  node_name: string;
  exit_code: number | null;
}
export interface Worker {
  id: string;
  capacity: Resources;
  reserved: Resources;
  drained: boolean;
  available: boolean;
  heartbeat_at: string;
  gpu_inventory: {
    capable: boolean;
    ready: boolean;
    fresh: boolean;
    observed_at: string;
    devices: Device[];
    available_estimate: number | null;
  };
}
export interface Capacity {
  backend: 'docker' | 'kubernetes';
  gpu: {
    capacity: number;
    allocatable: number;
    reserved: number;
    available_estimate: number | null;
    pending: number;
    fresh: boolean;
    observed_at: string;
    age_seconds: number | null;
  };
  workers: Worker[];
  nodes: {
    name: string;
    capacity: number;
    allocatable: number;
    reserved: number;
    ready: boolean;
    schedulable: boolean;
    eligible: boolean;
    available_estimate: number | null;
  }[];
  next_cursor: string;
}
export interface Telemetry {
  sequence: number;
  kind: 'stdout' | 'stderr' | 'notice' | 'metric';
  text: string;
  name: string;
  step: number;
  value: number;
  timestamp_ms: number;
}
export interface TelemetryPage {
  attempt_id: string;
  items: Telemetry[];
  next_cursor: number;
}
export interface Artifact {
  id: string;
  attempt_id: string;
  path: string;
  sha256: string;
  size: number;
}
export interface Event {
  sequence: number;
  kind: string;
  detail: string;
  created_at: string;
}
export interface Sweep {
  id: string;
  spec: { base: RunSpec; grid: Record<string, Scalar[]> };
  run_ids: string[];
  created_at: string;
}
export interface Page<T> {
  items: T[];
  next_cursor: string;
}
