CREATE TABLE workers (
  id text PRIMARY KEY,
  session text NOT NULL,
  cpu_millis integer NOT NULL CHECK(cpu_millis>0),
  memory_mib integer NOT NULL CHECK(memory_mib>0),
  drained boolean NOT NULL DEFAULT false,
  heartbeat_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
