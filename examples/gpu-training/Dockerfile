ARG RUNYARD_RUNNER_BASE=runyard-runner-base:dev
FROM ${RUNYARD_RUNNER_BASE}
ARG RUNYARD_RUNNER_BASE
USER root
RUN case "$RUNYARD_RUNNER_BASE" in *@sha256:*) ;; *) echo 'Pass an immutable runner-base image digest' >&2; exit 1 ;; esac \
    && test "$(dpkg --print-architecture)" = amd64 \
    && apt-get update && apt-get install -y --no-install-recommends python3 python3-venv \
    && rm -rf /var/lib/apt/lists/* && python3 -m venv /opt/venv
COPY requirements.lock /opt/experiment/requirements.lock
RUN /opt/venv/bin/pip install --no-cache-dir --require-hashes -r /opt/experiment/requirements.lock
COPY train.py /opt/experiment/train.py
ENV PATH=/opt/venv/bin:/usr/local/bin:/usr/bin:/bin PYTHONUNBUFFERED=1
USER 10001:10001
ENTRYPOINT ["/usr/local/bin/runyard-runner"]
