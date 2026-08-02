CREATE TABLE telemetry (
  attempt_id text NOT NULL REFERENCES attempts(id),
  sequence bigint NOT NULL,
  kind text NOT NULL CHECK(kind IN ('stdout','stderr','metric','notice')),
  text text NOT NULL DEFAULT '',
  name text NOT NULL DEFAULT '',
  step bigint NOT NULL DEFAULT 0,
  value double precision NOT NULL DEFAULT 0,
  timestamp_ms bigint NOT NULL,
  PRIMARY KEY(attempt_id,sequence)
);
CREATE INDEX metric_series ON telemetry(attempt_id,name,sequence) WHERE kind='metric';
