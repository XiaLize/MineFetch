$ErrorActionPreference = 'Stop'
$fixtureRoot = Join-Path $PSScriptRoot 'fixture'
$httpRoot = Join-Path $fixtureRoot 'http'
if (Test-Path $fixtureRoot) { Remove-Item -Recurse -Force $fixtureRoot }
New-Item -ItemType Directory -Force $httpRoot | Out-Null

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-Sha1([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA1).Hash.ToLowerInvariant()
}
function Get-Size([string]$Path) {
    (Get-Item -LiteralPath $Path).Length
}

$clientText = "fixture client jar`nthis is not a real jar, only a download test.`n"
$clientPath = Join-Path $httpRoot 'v1\objects\client\client.jar'
Write-Utf8NoBom $clientPath $clientText

$libText = "fixture library jar`n"
$libPath = Join-Path $httpRoot 'maven\com\example\testlib\1.0\testlib-1.0.jar'
Write-Utf8NoBom $libPath $libText

$logText = "<Configuration>fixture log config</Configuration>`n"
$logPath = Join-Path $httpRoot 'v1\objects\log\client-test.xml'
Write-Utf8NoBom $logPath $logText

$asset1Text = "hello from fixture asset one`n"
$asset1Path = Join-Path $httpRoot 'assets\objects\asset1'
Write-Utf8NoBom $asset1Path $asset1Text
$asset1Hash = Get-Sha1 $asset1Path
$asset1Size = Get-Size $asset1Path
$asset1TargetDir = Join-Path $httpRoot ("assets\" + $asset1Hash.Substring(0, 2))
New-Item -ItemType Directory -Force $asset1TargetDir | Out-Null
Copy-Item -LiteralPath $asset1Path -Destination (Join-Path $asset1TargetDir $asset1Hash) -Force

$asset2Text = "second fixture asset`n"
$asset2Path = Join-Path $httpRoot 'assets\objects\asset2'
Write-Utf8NoBom $asset2Path $asset2Text
$asset2Hash = Get-Sha1 $asset2Path
$asset2Size = Get-Size $asset2Path
$asset2TargetDir = Join-Path $httpRoot ("assets\" + $asset2Hash.Substring(0, 2))
New-Item -ItemType Directory -Force $asset2TargetDir | Out-Null
Copy-Item -LiteralPath $asset2Path -Destination (Join-Path $asset2TargetDir $asset2Hash) -Force

$assetIndexJson = @"
{
  "virtual": true,
  "map_to_resources": true,
  "objects": {
    "test/hello.txt": { "hash": "$asset1Hash", "size": $asset1Size },
    "test/nested/second.txt": { "hash": "$asset2Hash", "size": $asset2Size }
  }
}
"@
$assetIndexPath = Join-Path $httpRoot 'v1\packages\index\testassets.json'
Write-Utf8NoBom $assetIndexPath $assetIndexJson
$assetIndexHash = Get-Sha1 $assetIndexPath
$assetIndexSize = Get-Size $assetIndexPath

$clientHash = Get-Sha1 $clientPath
$clientSize = Get-Size $clientPath
$libHash = Get-Sha1 $libPath
$libSize = Get-Size $libPath
$logHash = Get-Sha1 $logPath
$logSize = Get-Size $logPath

$versionJson = @"
{
  "id": "testver",
  "type": "release",
  "mainClass": "net.minecraft.client.main.Main",
  "releaseTime": "2026-01-01T00:00:00+00:00",
  "time": "2026-01-01T00:00:00+00:00",
  "assets": "testassets",
  "assetIndex": {
    "id": "testassets",
    "sha1": "$assetIndexHash",
    "size": $assetIndexSize,
    "totalSize": 45,
    "url": "https://piston-meta.mojang.com/v1/packages/index/testassets.json"
  },
  "downloads": {
    "client": {
      "sha1": "$clientHash",
      "size": $clientSize,
      "url": "https://piston-data.mojang.com/redirect/client.jar"
    }
  },
  "libraries": [
    {
      "name": "com.example:testlib:1.0",
      "downloads": {
        "artifact": {
          "path": "com/example/testlib/1.0/testlib-1.0.jar",
          "sha1": "$libHash",
          "size": $libSize,
          "url": "https://libraries.minecraft.net/com/example/testlib/1.0/testlib-1.0.jar"
        }
      }
    }
  ],
  "logging": {
    "client": {
      "argument": "-Dlog4j.configurationFile=`${path}",
      "file": {
        "id": "client-test.xml",
        "sha1": "$logHash",
        "size": $logSize,
        "url": "https://piston-data.mojang.com/v1/objects/log/client-test.xml"
      },
      "type": "log4j2-xml"
    }
  }
}
"@
$versionPath = Join-Path $httpRoot 'v1\packages\test\testver.json'
Write-Utf8NoBom $versionPath $versionJson
$versionHash = Get-Sha1 $versionPath

$manifestJson = @"
{
  "latest": { "release": "testver", "snapshot": "testver" },
  "versions": [
    {
      "id": "testver",
      "type": "release",
      "url": "https://piston-meta.mojang.com/v1/packages/test/testver.json",
      "sha1": "$versionHash",
      "time": "2026-01-01T00:00:00+00:00",
      "releaseTime": "2026-01-01T00:00:00+00:00"
    }
  ]
}
"@
$manifestPath = Join-Path $httpRoot 'mc\game\version_manifest_v2.json'
Write-Utf8NoBom $manifestPath $manifestJson

Write-Output "fixture created at $fixtureRoot"
Write-Output "client sha1=$clientHash size=$clientSize"
Write-Output "version sha1=$versionHash"
