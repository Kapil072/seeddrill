import { store } from './dynamo.mjs';
import { ingest } from './records.mjs';

export const handler = (event) => ingest(event, store);
