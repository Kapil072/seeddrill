import { IoTDataPlaneClient, PublishCommand } from '@aws-sdk/client-iot-data-plane';
import { store } from './dynamo.mjs';
import { route } from './routes.mjs';

const iot = new IoTDataPlaneClient({});
const publish = (topic, msg) => iot.send(new PublishCommand({ topic, qos: 1, payload: Buffer.from(JSON.stringify(msg)) }));

export async function handler(event) {
  const claims = event.requestContext?.authorizer?.jwt?.claims ?? {};
  const groups = String(claims['cognito:groups'] ?? '');
  const r = await route({
    method: event.requestContext.http.method,
    path: event.rawPath,
    query: event.queryStringParameters ?? {},
    body: event.body ? JSON.parse(event.isBase64Encoded ? Buffer.from(event.body, 'base64').toString() : event.body) : null,
    user: { operator: groups.includes('operator') },
  }, { store, publish });

  const headers = { 'content-type': r.type ?? 'application/json', 'cache-control': 'no-store' };
  if (r.filename) headers['content-disposition'] = `attachment; filename="${r.filename}"`;
  return { statusCode: r.status, headers, body: typeof r.body === 'string' ? r.body : JSON.stringify(r.body) };
}
