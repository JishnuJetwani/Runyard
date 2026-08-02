CREATE TABLE artifacts (
  id text PRIMARY KEY,
  attempt_id text NOT NULL REFERENCES attempts(id),
  path text NOT NULL,
  storage_key text NOT NULL,
  sha256 text NOT NULL,
  size bigint NOT NULL CHECK(size>=0),
  UNIQUE(attempt_id,path)
);
