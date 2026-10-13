<#
.SYNOPSIS
  Vygeneruje ESP_Master/dashboard_html.h z Web_app/index.html.

.DESCRIPTION
  ESP_Master serviruje dashboard primo z flash pameti jako C++ raw string literal.
  Stranka deklaruje <meta charset="UTF-8"> a ESP posila hlavicku charset=utf-8, proto MUSI byt
  hlavickovy soubor ulozen jako UTF-8 (bez BOM). Drive rucne vytvorena kopie byla ve Windows-1250
  a ceska diakritika se v prohlizeci zobrazovala rozbite.

  Spust po KAZDE zmene index.html (pred kompilaci ESP_Master):
    powershell -NoProfile -ExecutionPolicy Bypass -File Web_app\build_dashboard.ps1
  nebo ve VS Code task "Dashboard: Generate ESP header".
#>
$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot 'index.html'
$dst = Join-Path (Split-Path $PSScriptRoot -Parent) 'ESP_Master\dashboard_html.h'

if (-not (Test-Path $src)) { throw "Nenalezen zdroj: $src" }

# ReadAllText s UTF8 encodingem automaticky odstrani BOM
$html = [System.IO.File]::ReadAllText($src, [System.Text.Encoding]::UTF8)
if ($html.Length -gt 0 -and [int][char]$html[0] -eq 0xFEFF) { $html = $html.Substring(1) }

if ($html.Contains(')rawliteral"')) {
    throw 'index.html obsahuje ukoncovaci sekvenci )rawliteral" - zmen delimiter raw string literalu.'
}

# Prefix/suffix jsou single-quoted here-stringy, aby PowerShell nerozbaloval $ a ` uvnitr HTML/JS
$prefix = @'
// ============================================================================
//  GENEROVANY SOUBOR - NEEDITOVAT RUCNE!
//  Zdroj:     Web_app/index.html
//  Generator: Web_app/build_dashboard.ps1   (spust po kazde zmene index.html)
//  Kodovani:  UTF-8 bez BOM (ESP stranku serviruje s charset=utf-8)
// ============================================================================
#pragma once
#include <stddef.h>

const char dashboard_html[] = R"rawliteral(
'@

$suffix = @'
)rawliteral";

// Delka HTML bez ukoncovaci nuly - pouzito pro hlavicku Content-Length
const size_t DASHBOARD_HTML_LEN = sizeof(dashboard_html) - 1;

'@

# Odstranime zalomeni, ktere here-string prida za uvodni "(" (jinak by stranka zacinala prazdnym radkem)
$prefix = $prefix.TrimEnd("`r", "`n")

# Sjednotime konce radku prefixu/suffixu s index.html (jinak by soubor mel smisene CRLF/LF)
$nl = if ($html.Contains("`r`n")) { "`r`n" } else { "`n" }
$prefix = $prefix -replace "`r?`n", $nl
$suffix = $suffix -replace "`r?`n", $nl

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($dst, ($prefix + $html + $suffix), $utf8NoBom)

$bytes = (Get-Item $dst).Length
Write-Host ("OK: {0} znaku HTML -> {1} ({2} bajtu, UTF-8)" -f $html.Length, $dst, $bytes)
