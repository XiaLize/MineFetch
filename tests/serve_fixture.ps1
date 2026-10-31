$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'fixture\http'))
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add('http://127.0.0.1:8765/')
try {
    $listener.Start()
} catch {
    Write-Error "无法启动 HttpListener: $($_.Exception.Message)"
    exit 1
}
Write-Output "fixture server listening on http://127.0.0.1:8765/"
$log = Join-Path $PSScriptRoot 'server.log'
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$rateLimitOnce = $true
try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response
        try {
            $relative = [System.Uri]::UnescapeDataString($request.Url.AbsolutePath).TrimStart('/')
            $relative = $relative.Replace('/', '\')
            Add-Content -LiteralPath $log -Value ("{0} {1}" -f $request.HttpMethod, $request.Url.AbsolutePath)
            if ($rateLimitOnce -and $relative -eq 'mc\game\version_manifest_v2.json') {
                $rateLimitOnce = $false
                $response.StatusCode = 429
                $response.Headers['Retry-After-short'] = '1'
                $body = [System.Text.Encoding]::UTF8.GetBytes('rate limited once')
                $response.ContentLength64 = $body.Length
                $response.OutputStream.Write($body, 0, $body.Length)
                $response.Close()
                continue
            }
            if ($relative -eq 'redirect\client.jar') {
                $response.StatusCode = 302
                $response.RedirectLocation = 'http://127.0.0.1:8765/v1/objects/client/client.jar'
                $response.ContentLength64 = 0
                $response.Close()
                continue
            }
            $full = [System.IO.Path]::GetFullPath((Join-Path $root $relative))
            if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase) -or
                -not (Test-Path -LiteralPath $full -PathType Leaf)) {
                $response.StatusCode = 404
                $body = [System.Text.Encoding]::UTF8.GetBytes('not found')
                $response.ContentLength64 = $body.Length
                $response.OutputStream.Write($body, 0, $body.Length)
                $response.Close()
                continue
            }
            $bytes = [System.IO.File]::ReadAllBytes($full)
            if ($full.EndsWith('.json')) {
                $response.ContentType = 'application/json; charset=utf-8'
            } else {
                $response.ContentType = 'application/octet-stream'
            }
            $response.StatusCode = 200
            $response.ContentLength64 = $bytes.Length
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
            $response.Close()
        } catch {
            try { $response.Abort() } catch { }
            Add-Content -LiteralPath $log -Value ("ERROR " + $_.Exception.Message)
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
