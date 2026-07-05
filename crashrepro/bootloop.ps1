$ErrorActionPreference = 'Continue'
$exe     = 'D:\RoboticsSoftware\build\debug\RoboticsSoftware.exe'
$wd      = 'D:\RoboticsSoftware\build\debug'
$logdir  = 'D:\RoboticsSoftware\crashrepro\logs'
$dumpdir = 'D:\RoboticsSoftware\crashdumps'
$status  = 'D:\RoboticsSoftware\crashrepro\status.log'
$maxRuns = 40
$targetCrashes = 2
$crashes = 0

function Log($msg) {
    $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $msg
    Add-Content -Path $status -Value $line -Encoding utf8
}

Set-Content $status "" -Encoding utf8
Log "bootloop start: maxRuns=$maxRuns targetCrashes=$targetCrashes"

for ($i = 1; $i -le $maxRuns; $i++) {
    $out = "$logdir\run$i.out.log"; $err = "$logdir\run$i.err.log"
    $preDumps = @(Get-ChildItem $dumpdir -Filter *.dmp -ErrorAction SilentlyContinue | ForEach-Object Name)
    $t0 = Get-Date
    $p = Start-Process -FilePath $exe -WorkingDirectory $wd -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    $deadline = $t0.AddSeconds(180)
    $result = 'HUNG'
    while ((Get-Date) -lt $deadline) {
        if ($p.HasExited) { $result = 'CRASH'; break }
        $p.Refresh()
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $result = 'WINDOW'; break }
        Start-Sleep -Milliseconds 250
    }
    $bootMs = [int]((Get-Date) - $t0).TotalMilliseconds

    if ($result -eq 'CRASH') {
        $crashes++
        $code = $p.ExitCode
        Log ("run {0}: BOOT CRASH after {1} ms, exit=0x{2:X8} ({2})" -f $i, $bootMs, $code)
        $dump = $null
        $dl = (Get-Date).AddSeconds(90)
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
        if ($dump) { Log "run ${i}: dump = $dump" } else { Log "run ${i}: NO DUMP appeared within 90 s" }
        $tail = Get-Content $err -Tail 6 -ErrorAction SilentlyContinue
        Log ("run {0}: stderr tail: {1}" -f $i, ($tail -join ' | '))
        if ($crashes -ge $targetCrashes) { Log "target crash count reached; stopping"; break }
    }
    elseif ($result -eq 'WINDOW') {
        Log ("run {0}: window OK after {1} ms; closing" -f $i, $bootMs)
        Start-Sleep -Seconds 2
        $p.CloseMainWindow() | Out-Null
        if (-not $p.WaitForExit(20000)) {
            Log "run ${i}: close timed out, killing"
            try { $p.Kill() } catch {}
            $p.WaitForExit(10000) | Out-Null
        }
        Start-Sleep -Seconds 1
    }
    else {
        Log ("run {0}: HUNG (no window, no exit in 180 s) - killing" -f $i)
        try { $p.Kill() } catch {}
        $p.WaitForExit(10000) | Out-Null
    }
}
Log "bootloop done: $crashes crash(es) in $i run(s)"
