// In-memory stand-in for DynamoDB with the same contract as src/dynamo.mjs.
export function memStore() {
  const rows = new Map();
  const id = (k) => `${k.pk}\u0000${k.sk}`;
  return {
    rows,
    async put(item) { rows.set(id(item), structuredClone(item)); },
    async get(k) { return rows.get(id(k)) ?? null; },
    async query(pk, prefix, { limit = 20, reverse = false, cursor } = {}) {
      let items = [...rows.values()].filter((r) => r.pk === pk && r.sk.startsWith(prefix)).sort((a, b) => (a.sk < b.sk ? -1 : 1));
      if (reverse) items.reverse();
      const start = cursor ? Number(cursor) : 0;
      const page = items.slice(start, start + limit);
      return { items: page, cursor: start + limit < items.length ? String(start + limit) : undefined };
    },
  };
}
