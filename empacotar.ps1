# empacotar.ps1 - gera build\BibliaARC1911-Windows.zip, pronto para anexar na release
# Uso: powershell -ExecutionPolicy Bypass -File empacotar.ps1
# O nome do arquivo precisa ser sempre BibliaARC1911-Windows.zip (o link do README usa esse nome).
# ATENÇÃO (28/11/2026): salve este arquivo sempre em UTF-8 com BOM. Sem o BOM o PowerShell 5 lê o texto como ANSI e os acentos do LEIA-ME.txt saem quebrados.

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$pasta = "build\pacote\BibliaARC1911"
$zip   = "build\BibliaARC1911-Windows.zip"

Remove-Item -Recurse -Force "build\pacote" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $pasta | Out-Null

# Só o que o usuário precisa: o app, a DLL do WebView2, a tela e os textos
Copy-Item "Biblia.exe", "WebView2Loader.dll" $pasta
Copy-Item "ui", "textos" $pasta -Recurse

$leia = @"
Bíblia Almeida ARC-1911 - Aplicativo para Windows

Para abrir: dê dois cliques em Biblia.exe.
Não precisa instalar e funciona sem internet.
Para levar no pendrive: copie esta pasta inteira.

Se aparecer 'O Windows protegeu o computador':
clique em 'Mais informações' e depois em 'Executar assim mesmo'.

Projeto aberto: https://github.com/RomuloCSR/biblia-almeida-1911-windows-app
"@
[IO.File]::WriteAllText("$PWD\$pasta\LEIA-ME.txt", $leia, (New-Object Text.UTF8Encoding($true)))

Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $pasta -DestinationPath $zip
Write-Host "Pronto: $PWD\$zip ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB)"
