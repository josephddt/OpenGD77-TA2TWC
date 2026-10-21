# Prints an HTML file of docs/ to an A4 PDF with the headless mode of Microsoft Edge.
#   powershell -File docs/make_pdf.ps1                                   # the guide
#   powershell -File docs/make_pdf.ps1 brosur.html MD-UV390_Brosur.pdf   # the brochure
# The work is done in a temporary folder, through its short (ASCII) path: Edge does not open every path with non ASCII characters.
param(
	[string]$Source = "kilavuz.html",
	[string]$Target = "MD-UV390_Ozel_Yazilim_Kilavuzu.pdf"
)

$docs = Split-Path -Parent $MyInvocation.MyCommand.Path
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$output = Join-Path $docs $Target

$work = Join-Path $env:TEMP "md390_guide_pdf"
# .NET calls: the cmdlets are refusing the short form of a path with non ASCII characters
if ([System.IO.Directory]::Exists($work)) { [System.IO.Directory]::Delete($work, $true) }
[System.IO.Directory]::CreateDirectory("$work\img") | Out-Null
[System.IO.File]::Copy((Join-Path $docs $Source), "$work\page.html")
foreach ($image in [System.IO.Directory]::GetFiles((Join-Path $docs "img"))) {
	[System.IO.File]::Copy($image, "$work\img\" + [System.IO.Path]::GetFileName($image))
}

$short = (New-Object -ComObject Scripting.FileSystemObject).GetFolder($work).ShortPath
$url = "file:///" + ($short -replace '\\', '/') + "/page.html"

& $edge --headless --disable-gpu --no-pdf-header-footer "--print-to-pdf=$short\out.pdf" $url 2>$null | Out-Null

# Edge returns before the file is written
for ($i = 0; ($i -lt 40) -and -not (Test-Path "$short\out.pdf"); $i++) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 1

Copy-Item "$short\out.pdf" $output -Force
Write-Output "Written: $output"
