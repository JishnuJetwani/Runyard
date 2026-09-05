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
