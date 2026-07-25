CREATE TABLE runs (
  id text PRIMARY KEY,
  spec jsonb NOT NULL,
  status text NOT NULL DEFAULT 'QUEUED' CHECK (status IN ('QUEUED','STARTING','RUNNING','FINALIZING','RETRY_WAIT','SUCCEEDED','FAILED','CANCELLED')),
  generation integer NOT NULL DEFAULT 0,
  active_attempt text,
  priority integer NOT NULL,
  cpu_millis integer NOT NULL CHECK(cpu_millis>0),
  memory_mib integer NOT NULL CHECK(memory_mib>0),
  sweep_id text,
  parent_run_id text REFERENCES runs(id),
  event_sequence bigint NOT NULL DEFAULT 0,
  available_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  updated_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
CREATE INDEX runnable_queue ON runs(priority DESC,created_at,id) WHERE status='QUEUED';
CREATE TABLE submissions(key text PRIMARY KEY, fingerprint text NOT NULL, run_id text NOT NULL REFERENCES runs(id));
CREATE TABLE attempts (
  id text PRIMARY KEY,
  run_id text NOT NULL REFERENCES runs(id),
  generation integer NOT NULL,
  worker_id text NOT NULL,
  instance_id text,
  status text NOT NULL DEFAULT 'STARTING',
  reason text,
  exit_code integer,
  runtime_id text,
  cleanup_status text NOT NULL DEFAULT 'PENDING',
  ack_sequence bigint NOT NULL DEFAULT 0,
  launch_deadline timestamptz NOT NULL,
  lease_until timestamptz,
  execution_deadline timestamptz,
  finalization_deadline timestamptz,
  created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  started_at timestamptz,
  finished_at timestamptz,
  UNIQUE(run_id,generation)
);
ALTER TABLE runs ADD CONSTRAINT active_attempt_fk FOREIGN KEY(active_attempt) REFERENCES attempts(id);
CREATE INDEX attempt_cleanup ON attempts(worker_id) WHERE cleanup_status='PENDING';
CREATE TABLE events (
  run_id text NOT NULL REFERENCES runs(id),
  sequence bigint NOT NULL,
  kind text NOT NULL,
  detail text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  PRIMARY KEY(run_id,sequence)
);
