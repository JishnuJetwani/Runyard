# Dashboard

The React/TypeScript dashboard uses the same `/v1` API as the CLI. It has pages for
experiments, comparison, and capacity. Run details show configuration, attempts,
logs, metrics, and artifacts. Scheduling stays in the backend.

## Design

The interface uses a narrow navigation rail and compact page headings.
Tables share column alignment, row separators, and actions.

| Role | Color |
|---|---|
| Canvas | `#f5f7f6` |
| Surface | `#ffffff` |
| Text | `#192a25` |
| Secondary text | `#65736d` |
| Border | `#dde4df` |
| Accent | `#28664f` |

The UI uses Manrope, with monospace for commands, IDs, and logs. Status labels
include text as well as color. Controls show keyboard focus, tables scroll on
small screens, and animations respect reduced-motion settings. Table controls
follow Carbon's [data-table guidance](https://carbondesignsystem.com/components/data-table/usage/).

## Development

With the backend running and its owner credential in the root `.env`:

```sh
cd ui
npm ci
npm run dev
```

Open `http://127.0.0.1:5180`. The loopback-only Vite development proxy reads
`RUNYARD_OWNER_TOKEN` on the server and forwards requests to the coordinator. The
credential never enters the JavaScript bundle. Set `RUNYARD_UI_API_URL` to use
another coordinator. Restart Vite after changing server configuration.

Without that credential, the dashboard asks for the owner's API key. A supplied
key is kept in tab-scoped session storage and sent only to same-origin `/v1`
requests. Do not use `VITE_` variables for credentials.

```sh
npm run build
npm test
```

## Workflows

- **Experiments:** filter by status, search loaded runs, and select up to four to
  compare. Counts and sorting cover loaded rows only. The server paginates by ID,
  so later pages may include newer runs.
- **New experiment:** enter or import a specification, choose resources, and
  submit a run or sweep. Retrying an unchanged submission reuses its request key.
- **Run details:** inspect any attempt's output, export configuration, cancel a
  run, rerun it, or use it as a template.
- **Compare:** view current-attempt metrics and parameter differences, with a
  table of plotted values.
- **Capacity:** inspect Docker workers or Kubernetes nodes, check GPU inventory,
  and drain or resume Docker workers. Stale availability is shown as unknown.

Log polling uses sequence cursors and removes duplicates. The view holds up to
2,000 chunks or two million characters, with each chunk capped at 200,000 characters.
Metrics keep the latest 10,000 samples per attempt. Truncation is marked, and exports
contain only the loaded data. Full published log archives are available as artifacts.
Changing attempts resets the cursor. Polling stops after a finished attempt's
saved output has been read.

Cancelled runs can still show pending cleanup until the process is confirmed stopped.

## Code organization

`src/lib` contains the API client, formatting, and telemetry polling.
`src/features` contains pages; `features/run` contains the run-detail tabs.
Shared UI components live in `src/components`. TanStack Query caches server data,
while form drafts stay in component state. Charts load on demand. Fonts are
self-hosted, and npm dependencies are locked.
