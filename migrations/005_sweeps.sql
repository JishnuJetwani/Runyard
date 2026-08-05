CREATE TABLE sweeps (
  id text PRIMARY KEY,
  spec jsonb NOT NULL,
  key text NOT NULL UNIQUE,
  fingerprint text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
ALTER TABLE runs ADD CONSTRAINT sweep_fk FOREIGN KEY(sweep_id) REFERENCES sweeps(id);
CREATE INDEX sweep_runs ON runs(sweep_id,id);
