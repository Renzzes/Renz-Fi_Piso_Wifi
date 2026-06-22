const pesoFormatter = new Intl.NumberFormat("en-PH", {
  style: "currency",
  currency: "PHP",
});

export function formatPeso(amount: number | undefined, loading = false): string {
  if (loading) return "Loading...";
  if (amount === undefined) return "—";
  return pesoFormatter.format(amount);
}
