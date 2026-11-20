# Registers one controller with AWS IoT and writes its identity to firmware\data.
# Then flash it with: cd firmware; pio run -t uploadfs
#   .\provision-device.ps1 -Thing rig-001 -Region ap-south-1
param(
  [Parameter(Mandatory)][string]$Thing,
  [string]$Region = 'ap-south-1',
  [string]$Stack = 'seeddrill'
)
$ErrorActionPreference = 'Stop'
$data = Join-Path $PSScriptRoot '..\firmware\data'
New-Item -ItemType Directory -Force $data | Out-Null

$policy = aws cloudformation describe-stacks --stack-name $Stack --region $Region --query "Stacks[0].Outputs[?OutputKey=='DevicePolicyName'].OutputValue" --output text
$endpoint = aws iot describe-endpoint --endpoint-type iot:Data-ATS --region $Region --query endpointAddress --output text

aws iot create-thing --thing-name $Thing --region $Region | Out-Null
$cert = aws iot create-keys-and-certificate --set-as-active --region $Region | ConvertFrom-Json
aws iot attach-policy --policy-name $policy --target $cert.certificateArn --region $Region
aws iot attach-thing-principal --thing-name $Thing --principal $cert.certificateArn --region $Region

# Written without a BOM: the firmware reads these byte for byte.
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $data 'device.crt'), $cert.certificatePem, $utf8)
[IO.File]::WriteAllText((Join-Path $data 'device.key'), $cert.keyPair.PrivateKey, $utf8)
[IO.File]::WriteAllText((Join-Path $data 'device.cfg'), "$endpoint`n$Thing`n", $utf8)
Invoke-WebRequest https://www.amazontrust.com/repository/AmazonRootCA1.pem -OutFile (Join-Path $data 'root.pem')

Write-Host "Provisioned $Thing. Private key is in firmware\data: keep it out of source control."
