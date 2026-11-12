# Copia as fontes Montserrat do projeto (negrito, com acentos) para a biblioteca
# lvgl do Arduino. Sem isso o firmware do ESP32 sai com a fonte original da LVGL,
# sem negrito e sem os caracteres acentuados (ã, ç, ê...).
#
# Rodar uma vez depois de instalar ou atualizar a biblioteca lvgl:
#   powershell -ExecutionPolicy Bypass -File tools\sync_lvgl_fonts.ps1

$ErrorActionPreference = 'Stop'

$cli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
$userDir = (& $cli config get directories.user | Out-String).Trim()
$source = Join-Path $PSScriptRoot '..\lv_port_pc_vscode\lvgl\src\font'
$target = Join-Path $userDir 'libraries\lvgl\src\font'

if (-not (Test-Path $target)) { throw "Biblioteca lvgl do Arduino nao encontrada em: $target" }

$copied = 0
Get-ChildItem $source -Filter 'lv_font_montserrat_*.c' | ForEach-Object {
    $destination = Join-Path $target $_.Name
    if (-not (Test-Path $destination) -or (Get-FileHash $_.FullName).Hash -ne (Get-FileHash $destination).Hash) {
        Copy-Item $_.FullName $destination -Force
        $copied++
    }
}

"Fontes copiadas: $copied (destino: $target)"
