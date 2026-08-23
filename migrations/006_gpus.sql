ALTER TABLE runs ADD COLUMN gpu_count integer NOT NULL DEFAULT 0 CHECK(gpu_count BETWEEN 0 AND 64);
CREATE INDEX runs_gpu_queue ON runs(gpu_count,priority DESC,created_at,id) WHERE status='QUEUED';
ALTER TABLE attempts ADD COLUMN gpu_count integer NOT NULL DEFAULT 0 CHECK(gpu_count BETWEEN 0 AND 64);
ALTER TABLE attempts ADD COLUMN node_name text NOT NULL DEFAULT '';
ALTER TABLE workers ADD COLUMN engine_id text NOT NULL DEFAULT '';
ALTER TABLE workers ADD COLUMN gpu_capable boolean NOT NULL DEFAULT false;
ALTER TABLE workers ADD COLUMN gpu_ready boolean NOT NULL DEFAULT false;
ALTER TABLE workers ADD COLUMN gpu_sequence bigint NOT NULL DEFAULT 0;
ALTER TABLE workers ADD COLUMN gpu_observed_at timestamptz;

CREATE TABLE gpu_devices (
  uuid text PRIMARY KEY,
  worker_id text NOT NULL REFERENCES workers(id),
  name text NOT NULL,
  memory_mib bigint NOT NULL CHECK(memory_mib>=0),
  eligible boolean NOT NULL,
  present boolean NOT NULL DEFAULT true,
  reason text NOT NULL DEFAULT '',
  observed_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
CREATE INDEX gpu_devices_worker ON gpu_devices(worker_id,uuid);
CREATE TABLE attempt_gpus (
  attempt_id text NOT NULL REFERENCES attempts(id),
  uuid text NOT NULL REFERENCES gpu_devices(uuid),
  name text NOT NULL,
  memory_mib bigint NOT NULL,
  allocated_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  released_at timestamptz,
  PRIMARY KEY(attempt_id,uuid)
);
-- A terminal decision cannot release a device while its runtime may still exist.
CREATE UNIQUE INDEX gpu_exclusive_allocation ON attempt_gpus(uuid) WHERE released_at IS NULL;
