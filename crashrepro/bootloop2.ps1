$ErrorActionPreference = 'Continue'
$exe     = 'D:\RoboticsSoftware\build\debug\RoboticsSoftware.exe'
$wd      = 'D:\RoboticsSoftware\build\debug'
$logdir  = 'D:\RoboticsSoftware\crashrepro\logs2'
$dumpdir = 'D:\RoboticsSoftware\crashdumps'
$status  = 'D:\RoboticsSoftware\crashrepro\status2.log'
$marker  = 'MainWindow constructed OK'
$maxRuns = 40
$targetCrashes = 2
$bootTimeoutSec = 300
$crashes = 0

New-Item -ItemType Directory -Force $logdir | Out-Null

function Log($msg) {
    $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $msg
    for ($try = 0; $try -lt 5; $try++) {
        try { Add-Content -Path $status -Value $line -Encoding utf8 -ErrorAction Stop; break }
        catch { Start-Sleep -Milliseconds 300 }
    }
}

Set-Content $status "" -Encoding utf8
Log "bootloop2 start: maxRuns=$maxRuns targetCrashes=$targetCrashes bootTimeout=${bootTimeoutSec}s"

for ($i = 1; $i -le $maxRuns; $i++) {
    $out = "$logdir\run$i.out.log"; $err = "$logdir\run$i.err.log"
    $preDumps = @(Get-ChildItem $dumpdir -Filter *.dmp -ErrorAction SilentlyContinue | ForEach-Object Name)
    $t0 = Get-Date
    $p = Start-Process -FilePath $exe -WorkingDirectory $wd -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    $deadline = $t0.AddSeconds($bootTimeoutSec)
    $result = 'HUNG'
    while ((Get-Date) -lt $deadline) {
        if ($p.HasExited) { $result = 'CRASH'; break }
        # Boot is complete only when main.cpp prints the marker to stderr.
        if (Select-String -Path $err -Pattern $marker -SimpleMatch -Quiet -ErrorAction SilentlyContinue) {
            $result = 'BOOTED'; break
        }
        Start-Sleep -Milliseconds 500
    }
    $bootSec = [int]((Get-Date) - $t0).TotalSeconds

    if ($result -eq 'CRASH') {
        $crashes++
        $code = $p.ExitCode
        Log ("run {0}: BOOT CRASH after {1} s, exit=0x{2:X8} ({2})" -f $i, $bootSec, $code)
        $dump = $null
        $dl = (Get-Date).AddSeconds(120)
        while ((Get-Date) -lt $dl) {
            $new = Get-ChildItem $dumpdir -Filter *.dmp -ErrorAction SilentlyContinue | Where-Object { $preDumps -notcontains $_.Name }
            if ($new) {
                $d = $new | Select-Object -First 1
                $s1 = $d.Length
                Start-Sleep -Seconds 3
                $s2 = (Get-Item $d.FullName).Length
                if ($s1 -eq $s2 -and $s1 -gt 0) { $dump = $d.FullName; break }
            } else {
                Start-Sleep -Seconds 2
            }
        }
        if ($dump) { Log "run ${i}: dump = $dump" } else { Log "run ${i}: NO DUMP appeared within 120 s" }
        $tail = Get-Content $err -Tail 6 -ErrorAction SilentlyContinue
        Log ("run {0}: stderr tail: {1}" -f $i, ($tail -join ' | '))
        if ($crashes -ge $targetCrashes) { Log "target crash count reached; stopping"; break }
    }
    elseif ($result -eq 'BOOTED') {
        Log ("run {0}: booted OK in {1} s; closing" -f $i, $bootSec)
        Start-Sleep -Seconds 3
        $p.CloseMainWindow() | Out-Null
        if (-not $p.WaitForExit(45000)) {
            Log "run ${i}: graceful close timed out after 45 s, killing"
            try { $p.Kill() } catch {}
            $p.WaitForExit(10000) | Out-Null
        }
        Start-Sleep -Seconds 1
    }
    else {
        Log ("run {0}: HUNG (no marker, no exit in {1} s) - killing" -f $i, $bootTimeoutSec)
        try { $p.Kill() } catch {}
        $p.WaitForExit(10000) | Out-Null
    }
}
Log "bootloop2 done: $crashes crash(es)"
