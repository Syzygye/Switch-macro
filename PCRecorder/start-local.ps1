$ErrorActionPreference = "Stop"
$port = 8765

$pythonLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($pythonLauncher) {
    $pythonPath = $pythonLauncher.Source
    $arguments = @("-3", "-m", "http.server", "$port", "--bind", "127.0.0.1", "--directory", "`"$PSScriptRoot`"")
} else {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if (-not $pythonCommand) {
        throw "Instala Python 3 para iniciar el grabador local."
    }
    $pythonPath = $pythonCommand.Source
    $arguments = @("-m", "http.server", "$port", "--bind", "127.0.0.1", "--directory", "`"$PSScriptRoot`"")
}

$server = Start-Process -FilePath $pythonPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Milliseconds 700
    if ($server.HasExited) {
        throw "No se pudo iniciar el servidor local en el puerto $port."
    }
    Start-Process "http://127.0.0.1:$port/"
    Write-Host "Grabador abierto en http://127.0.0.1:$port/ . Pulsa Ctrl+C para cerrarlo."
    Wait-Process -Id $server.Id
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
    }
}
