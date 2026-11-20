import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { DynamoDBDocumentClient, GetCommand, PutCommand, QueryCommand } from '@aws-sdk/lib-dynamodb';

const doc = DynamoDBDocumentClient.from(new DynamoDBClient({}), { marshallOptions: { removeUndefinedValues: true } });
const TableName = process.env.TABLE;

const encode = (k) => (k ? Buffer.from(JSON.stringify(k)).toString('base64url') : undefined);
const decode = (c) => (c ? JSON.parse(Buffer.from(c, 'base64url').toString()) : undefined);

export const store = {
  put: (item) => doc.send(new PutCommand({ TableName, Item: item })),
  get: async ({ pk, sk }) => (await doc.send(new GetCommand({ TableName, Key: { pk, sk } }))).Item ?? null,
  async query(pk, prefix, { limit = 20, reverse = false, cursor } = {}) {
    const r = await doc.send(new QueryCommand({
      TableName,
      KeyConditionExpression: prefix ? 'pk = :p AND begins_with(sk, :s)' : 'pk = :p',
      ExpressionAttributeValues: prefix ? { ':p': pk, ':s': prefix } : { ':p': pk },
      ScanIndexForward: !reverse,
      Limit: limit,
      ExclusiveStartKey: decode(cursor),
    }));
    return { items: r.Items ?? [], cursor: encode(r.LastEvaluatedKey) };
  },
};
