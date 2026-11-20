# Deploys the AWS stack and the dashboard. Needs the AWS CLI and SAM CLI, signed in.
#   .\deploy.ps1 -Region ap-south-1
param(
  [string]$Region = 'ap-south-1',
  [string]$Stack = 'seeddrill'
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

sam build --template template.yaml
sam deploy --stack-name $Stack --region $Region --resolve-s3 --capabilities CAPABILITY_IAM --no-confirm-changeset --no-fail-on-empty-changeset

$out = @{}
(aws cloudformation describe-stacks --stack-name $Stack --region $Region --query 'Stacks[0].Outputs' | ConvertFrom-Json) |
  ForEach-Object { $out[$_.OutputKey] = $_.OutputValue }

Set-Location ..\dashboard
@"
VITE_API_URL=$($out.ApiUrl)
VITE_COGNITO_REGION=$Region
VITE_COGNITO_CLIENT_ID=$($out.UserPoolClientId)
"@ | Out-File -Encoding ascii .env.production
npm install
npm run build

# Hashed assets can be cached for a year; the entry points must always revalidate.
aws s3 sync dist "s3://$($out.SiteBucket)" --delete --region $Region --exclude 'index.html' --exclude 'sw.js' --cache-control 'public,max-age=31536000,immutable'
aws s3 cp dist/index.html "s3://$($out.SiteBucket)/index.html" --cache-control 'no-cache' --region $Region
aws s3 cp dist/sw.js "s3://$($out.SiteBucket)/sw.js" --cache-control 'no-cache' --region $Region
aws cloudfront create-invalidation --distribution-id $out.CdnId --paths '/index.html' '/sw.js' | Out-Null

Write-Host ""
Write-Host "Dashboard: $($out.DashboardUrl)"
Write-Host "Add users: aws cognito-idp admin-create-user --user-pool-id $($out.UserPoolId) --username you@example.com --region $Region"
Write-Host "Allow control: aws cognito-idp admin-add-user-to-group --user-pool-id $($out.UserPoolId) --username you@example.com --group-name operator --region $Region"
