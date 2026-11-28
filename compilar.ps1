# compilar.ps1 - gera o Biblia.exe nesta pasta
# Uso: powershell -ExecutionPolicy Bypass -File compilar.ps1

$ErrorActionPreference = "Stop"
$raiz = $PSScriptRoot
Set-Location $raiz
New-Item -ItemType Directory -Force "build" | Out-Null

# 1. Kernel da Bíblia (28/11/2026). Gerado a partir de motor\motor.biblia.c; mesmo texto, mais rápido.
#    Para mudar uma regra: edite motor\motor.biblia.c e gere o kernel de novo (veja kernel\LEIAME.md).
gcc -O2 -Wall -Wextra -c "kernel\biblia_kernel.c" -o "build\kernel.o"
if ($LASTEXITCODE -ne 0) { throw "Erro ao compilar o kernel." }

# 2. Janela do aplicativo
g++ -O2 -std=c++17 -Wall -Ikit -c "src\app.cpp" -o "build\app.o"
if ($LASTEXITCODE -ne 0) { throw "Erro ao compilar o aplicativo." }

# 3. Juntar tudo num .exe sem depender de DLLs do compilador
g++ -o "Biblia.exe" "build\app.o" "build\kernel.o" -mwindows -municode -static -static-libgcc -static-libstdc++ -lole32 -lshell32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "Erro ao gerar o Biblia.exe." }

# 4. O WebView2Loader.dll precisa ficar ao lado do Biblia.exe
if (-not (Test-Path "WebView2Loader.dll") -or
    (Get-FileHash "kit\WebView2Loader.dll").Hash -ne (Get-FileHash "WebView2Loader.dll").Hash) {
    Copy-Item "kit\WebView2Loader.dll" "WebView2Loader.dll" -Force
}

Write-Host "Pronto: $raiz\Biblia.exe"
