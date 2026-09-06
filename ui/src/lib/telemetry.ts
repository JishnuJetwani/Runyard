import { useEffect, useState } from 'react';
import { api } from './api';
import type { Telemetry } from './types';

const empty = { items: [] as Telemetry[], loading: true, trimmed: false, error: null as unknown };
export function useTelemetry(
  kind: 'logs' | 'metrics',
  run: string,
  attempt: string,
  live: boolean,
) {
  const [state, setState] = useState(empty);
  const [revision, setRevision] = useState(0);
  useEffect(() => {
    setState({ ...empty, loading: Boolean(attempt) });
    if (!attempt) return;
    const controller = new AbortController();
    let cursor = 0;
    let timer: ReturnType<typeof setTimeout>;
    async function poll() {
      try {
        const page = await api.telemetry(kind, run, attempt, cursor, controller.signal);
        if (controller.signal.aborted) return;
        if (page.attempt_id !== attempt)
          throw new Error('Telemetry ownership changed. Reload this attempt.');
        const previous = cursor;
        cursor = Math.max(cursor, page.next_cursor);
        setState((current) => {
          const items = [
            ...current.items,
            ...page.items.filter((item) => item.sequence > previous),
          ];
          const max = kind === 'logs' ? 2000 : 10000;
          let trimmed = current.trimmed || items.length > max;
          const bounded = items.slice(-max);
          if (kind === 'logs') {
            // A cursor acknowledges persisted batches, even when the view discards
            // older text. Dropping rendered history must never rewind ingestion.
            let characters = 0;
            for (let index = bounded.length - 1; index >= 0; index--) {
              const item = bounded[index];
              if (item.text.length > 200_000) {
                bounded[index] = { ...item, text: item.text.slice(-200_000) };
                trimmed = true;
              }
              characters += bounded[index].text.length;
              if (characters > 2_000_000) {
                bounded.splice(0, index + 1);
                trimmed = true;
                break;
              }
            }
          }
          return { items: bounded, loading: false, trimmed, error: null };
        });
        if (live || (page.items.length === 1000 && cursor > previous))
          timer = setTimeout(poll, page.items.length === 1000 ? 50 : 1000);
      } catch (error) {
        if (!controller.signal.aborted) {
          setState((current) => ({ ...current, loading: false, error }));
          if (live) timer = setTimeout(poll, 5000);
        }
      }
    }
    void poll();
    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [kind, run, attempt, live, revision]);
  return { ...state, retry: () => setRevision((value) => value + 1) };
}
