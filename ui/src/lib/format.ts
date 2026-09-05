export function date(value: string) {
  if (!value) return 'N/A';
  const parsed = new Date(value.replace(' ', 'T').replace(/(\.\d{3})\d+/, '$1').replace(/([+-]\d{2})$/, '$1:00'));
  return Number.isNaN(parsed.valueOf()) ? value : parsed.toLocaleString(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
}
export const shortId = (value: string) => value.length > 12 ? value.slice(0, 8) : value;
export const number = (value: number) => new Intl.NumberFormat(undefined, { maximumFractionDigits: 3 }).format(value);
export const bytes = (value: number) => value >= 1024 ** 2 ? `${number(value / 1024 ** 2)} MB` : value >= 1024 ? `${number(value / 1024)} KB` : `${value} B`;
export const words = (value: string) => value.toLowerCase().replaceAll('_', ' ');
