param([int]$Port=8765)
$ErrorActionPreference='Stop'
if($Port -lt 1024 -or $Port -gt 65535){throw 'Choose a local unprivileged port from 1024 to 65535'}
$root = Split-Path $PSScriptRoot -Parent
$content = [IO.File]::ReadAllBytes((Join-Path $root 'tests\receiver.html'))
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,$Port)
$listener.Start()
Write-Host "Local receiver: http://127.0.0.1:$Port/ (Ctrl+C stops the server)"
try {
    while($true){
        $client = $listener.AcceptTcpClient()
        try {
            $client.ReceiveTimeout=2000
            $stream = $client.GetStream()
            $reader = [IO.StreamReader]::new($stream,[Text.Encoding]::ASCII,$false,1024,$true)
            $request = $reader.ReadLine()
            do { $line=$reader.ReadLine() } while($line)
            if($request -match '^GET /(?:receiver\.html)?(?:\?[^ ]*)? HTTP/1\.[01]$'){
                $header=[Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: text/html; charset=utf-8`r`nContent-Length: $($content.Length)`r`nCache-Control: no-store`r`nConnection: close`r`n`r`n")
                $stream.Write($header,0,$header.Length)
                $stream.Write($content,0,$content.Length)
            }else{
                $header=[Text.Encoding]::ASCII.GetBytes("HTTP/1.1 404 Not Found`r`nContent-Length: 0`r`nConnection: close`r`n`r`n")
                $stream.Write($header,0,$header.Length)
            }
            $stream.Flush()
        }catch{Write-Warning 'A local browser connection ended before the page was served'}
        finally{$client.Dispose()}
    }
} finally {$listener.Stop()}
