param(
    [Parameter(Mandatory=$true)][string]$DumpPath,
    [string]$OutFile = ''
)
if (-not $OutFile) { $OutFile = [System.IO.Path]::ChangeExtension($DumpPath, '.analysis.txt') }
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$sym = 'D:\RoboticsSoftware\build\debug;cache*D:\RoboticsSoftware\crashrepro\symcache;srv*https://msdl.microsoft.com/download/symbols'
& $cdb -z $DumpPath -y $sym -lines -c "!analyze -v; .ecxr; kL 60; q" > $OutFile 2>&1
Write-Output "analysis written to $OutFile"
