import { useState } from 'react';
import {
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { Empty } from './common';
import { number } from '../lib/format';
import type { Telemetry } from '../lib/types';

export const chartColors = ['#28664f', '#5b7baa', '#b67b3c', '#9c6997'];
export interface MetricSeries {
  id: string;
  name: string;
  items: Telemetry[];
}
export function MetricChart({ series }: { series: MetricSeries[] }) {
  const names = [
    ...new Set(series.flatMap((item) => item.items.map((sample) => sample.name))),
  ].sort();
  const [selected, setSelected] = useState('');
  const metric = names.includes(selected) ? selected : names[0] || '';
  const rows = new Map<number, Record<string, number>>();
  series.forEach((entry, index) =>
    entry.items
      .filter((item) => item.name === metric)
      .forEach((item) => {
        const row = rows.get(item.step) || { step: item.step };
        row[`value${index}`] = item.value;
        rows.set(item.step, row);
      }),
  );
  const data = [...rows.values()].sort((a, b) => a.step - b.step);
  if (!names.length)
    return (
      <Empty title="No metrics yet">
        Metric records will appear here when the experiment reports them.
      </Empty>
    );
  return (
    <div className="metric-chart">
      <div className="chart-toolbar">
        <label>
          Metric
          <select
            aria-label="Chart metric"
            value={metric}
            onChange={(event) => setSelected(event.target.value)}
          >
            {names.map((name) => (
              <option key={name}>{name}</option>
            ))}
          </select>
        </label>
        <span>Step →</span>
      </div>
      <div
        className="chart-canvas"
        role="img"
        aria-label={`${metric} by step for ${series.map((item) => item.name).join(', ')}`}
      >
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data} margin={{ top: 10, right: 24, left: 0, bottom: 10 }}>
            <CartesianGrid stroke="#e8eeea" vertical={false} />
            <XAxis
              dataKey="step"
              type="number"
              domain={['dataMin', 'dataMax']}
              axisLine={false}
              tickLine={false}
              tick={{ fill: '#77847c', fontSize: 10 }}
              minTickGap={30}
            />
            <YAxis
              width={58}
              axisLine={false}
              tickLine={false}
              tick={{ fill: '#77847c', fontSize: 10 }}
              tickFormatter={number}
              domain={['auto', 'auto']}
            />
            <Tooltip
              labelFormatter={(label) => `Step ${label}`}
              contentStyle={{ fontSize: 12, border: '1px solid #dde4df', borderRadius: 6 }}
            />
            {series.map((entry, index) => (
              <Line
                key={entry.id}
                name={entry.name}
                type="linear"
                dataKey={`value${index}`}
                stroke={chartColors[index % chartColors.length]}
                strokeWidth={2}
                dot={data.length < 40 ? { r: 2.5, strokeWidth: 0 } : false}
                activeDot={{ r: 4 }}
                connectNulls
                isAnimationActive={false}
              />
            ))}
          </LineChart>
        </ResponsiveContainer>
      </div>
      <div className="chart-legend">
        {series.map((entry, index) => {
          const values = entry.items.filter((item) => item.name === metric);
          return (
            <div key={entry.id}>
              <span style={{ background: chartColors[index % chartColors.length] }} />
              <span>{entry.name}</span>
              <strong>{values.length ? number(values.at(-1)!.value) : 'N/A'}</strong>
            </div>
          );
        })}
      </div>
      <details className="chart-values">
        <summary>View chart data</summary>
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Step</th>
                {series.map((entry) => (
                  <th key={entry.id}>{entry.name}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {data.slice(-100).map((row) => (
                <tr key={row.step}>
                  <td>{row.step}</td>
                  {series.map((entry, index) => (
                    <td key={entry.id}>
                      {row[`value${index}`] == null ? 'N/A' : number(row[`value${index}`])}
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="help">Latest 100 plotted steps. Export metrics for the full loaded set.</p>
      </details>
    </div>
  );
}
