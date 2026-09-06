import { defineConfig, loadEnv } from 'vite';
import react from '@vitejs/plugin-react';
import { fileURLToPath } from 'node:url';

export default defineConfig(({ mode }) => {
  const root = fileURLToPath(new URL('..', import.meta.url));
  const env = { ...loadEnv(mode, root, 'RUNYARD_'), ...process.env };
  const target = env.RUNYARD_UI_API_URL || 'http://127.0.0.1:8080';
  return {
    plugins: [
      react(),
      {
        name: 'private-local-gateway',
        configureServer(server) {
          server.middlewares.use((req, res, next) => {
            if (!req.url?.startsWith('/v1/')) return next();
            const origin = req.headers.origin;
            // The development proxy holds an owner credential. Only this local UI
            // may make browser requests; mutations must use the JSON API contract.
            if (
              (origin && new URL(origin).host !== req.headers.host) ||
              (req.method === 'POST' &&
                !req.headers['content-type']?.startsWith('application/json'))
            ) {
              res.statusCode = 403;
              res.end('Use the same-origin JSON API');
              return;
            }
            next();
          });
        },
      },
    ],
    server: {
      host: '127.0.0.1',
      port: 5180,
      strictPort: true,
      proxy: {
        '/v1': {
          target,
          configure(proxy) {
            proxy.on('proxyReq', (request, incoming) => {
              if (!incoming.headers.authorization && env.RUNYARD_OWNER_TOKEN) {
                request.setHeader('Authorization', `Bearer ${env.RUNYARD_OWNER_TOKEN}`);
              }
            });
          },
        },
        '/health': { target },
      },
    },
    build: { chunkSizeWarningLimit: 700 },
  };
});
