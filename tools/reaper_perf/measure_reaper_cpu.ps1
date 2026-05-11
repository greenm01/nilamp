# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$ProcessName = "reaper",
    [int]$ReaperPid = 0,
    [string]$MarkerPath = "",
    [string]$OutDir = "dist\reaper-perf",
    [int]$PollMs = 250,
    [int]$TimeoutSeconds = 900,
    [double]$WarmupTrimSeconds = 2.0,
    [double]$CooldownTrimSeconds = 1.0,
    [switch]$KeepExistingMarkers
)

$ErrorActionPreference = "Stop"

if (-not $MarkerPath) {
    $MarkerPath = Join-Path ([System.IO.Path]::GetTempPath()) "nilamp_reaper_perf_markers.jsonl"
}

function Resolve-ReaperProcess {
    if ($ReaperPid -gt 0) {
        return Get-Process -Id $ReaperPid -ErrorAction Stop
    }

    $matches = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
    if ($matches.Count -eq 0) {
        throw "No process named '$ProcessName' is running. Start REAPER first, or pass -ReaperPid."
    }
    if ($matches.Count -gt 1) {
        $ids = ($matches | ForEach-Object { "$($_.Id):$($_.ProcessName)" }) -join ", "
        throw "Multiple '$ProcessName' processes found ($ids). Pass -ReaperPid."
    }
    return $matches[0]
}

function Percentile {
    param(
        [double[]]$Values,
        [double]$Percent
    )
    if ($Values.Count -eq 0) {
        return $null
    }
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 1) {
        return [double]$sorted[0]
    }
    $rank = ($Percent / 100.0) * ($sorted.Count - 1)
    $lo = [math]::Floor($rank)
    $hi = [math]::Ceiling($rank)
    if ($lo -eq $hi) {
        return [double]$sorted[$lo]
    }
    $weight = $rank - $lo
    return [double]$sorted[$lo] * (1.0 - $weight) + [double]$sorted[$hi] * $weight
}

function Average {
    param([double[]]$Values)
    if ($Values.Count -eq 0) {
        return $null
    }
    $sum = 0.0
    foreach ($value in $Values) {
        $sum += $value
    }
    return $sum / $Values.Count
}

function Convert-MarkerLine {
    param([string]$Line)
    try {
        return $Line | ConvertFrom-Json -ErrorAction Stop
    } catch {
        Write-Warning "Ignoring malformed marker line: $Line"
        return $null
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not $KeepExistingMarkers) {
    Remove-Item -LiteralPath $MarkerPath -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $MarkerPath -PathType Leaf)) {
    New-Item -ItemType File -Force -Path $MarkerPath | Out-Null
}

$process = Resolve-ReaperProcess
$logicalProcessors = [Environment]::ProcessorCount
$sessionStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$rawCsv = Join-Path $OutDir "reaper-cpu-samples-$sessionStamp.csv"
$rawJson = Join-Path $OutDir "reaper-cpu-samples-$sessionStamp.json"
$markerJson = Join-Path $OutDir "reaper-markers-$sessionStamp.json"
$summaryCsv = Join-Path $OutDir "reaper-cpu-summary-$sessionStamp.csv"
$summaryJson = Join-Path $OutDir "reaper-cpu-summary-$sessionStamp.json"

Write-Host "Sampling REAPER process $($process.Id) every $PollMs ms."
Write-Host "Marker file: $MarkerPath"
Write-Host "Run tools/reaper_perf/run_reaper_perf_scenarios.lua inside REAPER now."

$samples = New-Object System.Collections.Generic.List[object]
$markers = New-Object System.Collections.Generic.List[object]
$lineCount = 0
$done = $false
$started = [DateTimeOffset]::UtcNow
$lastTime = $started
$lastCpu = $process.TotalProcessorTime.TotalSeconds

while (-not $done) {
    Start-Sleep -Milliseconds $PollMs
    $now = [DateTimeOffset]::UtcNow

    $process.Refresh()
    if ($process.HasExited) {
        throw "REAPER process $($process.Id) exited while sampling."
    }

    $cpu = $process.TotalProcessorTime.TotalSeconds
    $elapsed = ($now - $lastTime).TotalSeconds
    $cpuDelta = $cpu - $lastCpu
    $oneCorePct = if ($elapsed -gt 0.0) { 100.0 * $cpuDelta / $elapsed } else { 0.0 }
    $totalCapacityPct = $oneCorePct / $logicalProcessors

    $samples.Add([pscustomobject]@{
        time_utc = $now.ToString("o")
        elapsed_s = ($now - $started).TotalSeconds
        process_id = $process.Id
        logical_processors = $logicalProcessors
        cpu_total_s = $cpu
        cpu_delta_s = $cpuDelta
        cpu_pct_one_core = $oneCorePct
        cpu_pct_total_capacity = $totalCapacityPct
        working_set_bytes = $process.WorkingSet64
    }) | Out-Null

    $lastTime = $now
    $lastCpu = $cpu

    $lines = @(Get-Content -LiteralPath $MarkerPath -ErrorAction SilentlyContinue)
    while ($lineCount -lt $lines.Count) {
        $line = $lines[$lineCount]
        $lineCount += 1
        if (-not $line) {
            continue
        }
        $marker = Convert-MarkerLine $line
        if ($null -eq $marker) {
            continue
        }
        $event = [string]$marker.event
        $markers.Add([pscustomobject]@{
            observed_utc = $now.ToString("o")
            event = $event
            scenario = [string]$marker.scenario
            surface = [string]$marker.surface
            editor = [string]$marker.editor
            run = [int]($marker.run -as [int])
            fx_name = [string]$marker.fx_name
            marker = $marker
        }) | Out-Null
        if ($event -eq "done") {
            $done = $true
        }
    }

    if (($now - $started).TotalSeconds -gt $TimeoutSeconds) {
        throw "Timed out after $TimeoutSeconds seconds waiting for a 'done' marker."
    }
}

$intervals = New-Object System.Collections.Generic.List[object]
$openStarts = @{}
foreach ($marker in $markers) {
    $key = "$($marker.scenario)|$($marker.run)"
    if ($marker.event -eq "start") {
        $openStarts[$key] = $marker
    } elseif ($marker.event -eq "end" -and $openStarts.ContainsKey($key)) {
        $start = $openStarts[$key]
        $intervals.Add([pscustomobject]@{
            scenario = $marker.scenario
            surface = $marker.surface
            editor = $marker.editor
            run = $marker.run
            fx_name = $marker.fx_name
            start_utc = $start.observed_utc
            end_utc = $marker.observed_utc
        }) | Out-Null
        $openStarts.Remove($key)
    }
}

$summary = New-Object System.Collections.Generic.List[object]
$aggregates = @{}
foreach ($interval in $intervals) {
    $startTime = [DateTimeOffset]::Parse($interval.start_utc).AddSeconds($WarmupTrimSeconds)
    $endTime = [DateTimeOffset]::Parse($interval.end_utc).AddSeconds(-$CooldownTrimSeconds)
    $windowSamples = @(
        $samples | Where-Object {
            $t = [DateTimeOffset]::Parse($_.time_utc)
            $t -ge $startTime -and $t -le $endTime
        }
    )
    $values = [double[]]@($windowSamples | ForEach-Object { [double]$_.cpu_pct_total_capacity })
    $oneCoreValues = [double[]]@($windowSamples | ForEach-Object { [double]$_.cpu_pct_one_core })
    if (-not $aggregates.ContainsKey($interval.scenario)) {
        $aggregates[$interval.scenario] = [pscustomobject]@{
            surface = $interval.surface
            editor = $interval.editor
            fx_name = $interval.fx_name
            intervals = 0
            values = New-Object System.Collections.Generic.List[double]
            one_core_values = New-Object System.Collections.Generic.List[double]
        }
    }
    $aggregate = $aggregates[$interval.scenario]
    $aggregate.intervals += 1
    foreach ($value in $values) {
        $aggregate.values.Add($value) | Out-Null
    }
    foreach ($value in $oneCoreValues) {
        $aggregate.one_core_values.Add($value) | Out-Null
    }
    $summary.Add([pscustomobject]@{
        row_type = "run"
        scenario = $interval.scenario
        surface = $interval.surface
        editor = $interval.editor
        run = $interval.run
        fx_name = $interval.fx_name
        samples = $values.Count
        start_utc = $interval.start_utc
        end_utc = $interval.end_utc
        warmup_trim_s = $WarmupTrimSeconds
        cooldown_trim_s = $CooldownTrimSeconds
        mean_cpu_pct_total_capacity = Average $values
        median_cpu_pct_total_capacity = Percentile $values 50
        p95_cpu_pct_total_capacity = Percentile $values 95
        max_cpu_pct_total_capacity = if ($values.Count -gt 0) { ($values | Measure-Object -Maximum).Maximum } else { $null }
        mean_cpu_pct_one_core = Average $oneCoreValues
        median_cpu_pct_one_core = Percentile $oneCoreValues 50
        p95_cpu_pct_one_core = Percentile $oneCoreValues 95
        max_cpu_pct_one_core = if ($oneCoreValues.Count -gt 0) { ($oneCoreValues | Measure-Object -Maximum).Maximum } else { $null }
    }) | Out-Null
}

foreach ($scenario in ($aggregates.Keys | Sort-Object)) {
    $aggregate = $aggregates[$scenario]
    $values = [double[]]$aggregate.values.ToArray()
    $oneCoreValues = [double[]]$aggregate.one_core_values.ToArray()
    $summary.Add([pscustomobject]@{
        row_type = "aggregate"
        scenario = $scenario
        surface = $aggregate.surface
        editor = $aggregate.editor
        run = 0
        fx_name = $aggregate.fx_name
        samples = $values.Count
        start_utc = ""
        end_utc = ""
        warmup_trim_s = $WarmupTrimSeconds
        cooldown_trim_s = $CooldownTrimSeconds
        mean_cpu_pct_total_capacity = Average $values
        median_cpu_pct_total_capacity = Percentile $values 50
        p95_cpu_pct_total_capacity = Percentile $values 95
        max_cpu_pct_total_capacity = if ($values.Count -gt 0) { ($values | Measure-Object -Maximum).Maximum } else { $null }
        mean_cpu_pct_one_core = Average $oneCoreValues
        median_cpu_pct_one_core = Percentile $oneCoreValues 50
        p95_cpu_pct_one_core = Percentile $oneCoreValues 95
        max_cpu_pct_one_core = if ($oneCoreValues.Count -gt 0) { ($oneCoreValues | Measure-Object -Maximum).Maximum } else { $null }
    }) | Out-Null
}

$samples | Export-Csv -NoTypeInformation -LiteralPath $rawCsv
$samples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $rawJson -Encoding UTF8
$markers | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $markerJson -Encoding UTF8
$summary | Export-Csv -NoTypeInformation -LiteralPath $summaryCsv
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryJson -Encoding UTF8

Write-Host ""
Write-Host "Aggregate scenario summary, CPU normalized to total logical CPU capacity:"
$summary |
    Where-Object { $_.row_type -eq "aggregate" } |
    Sort-Object scenario |
    Select-Object scenario, samples,
        @{Name = "median_cpu_pct"; Expression = { "{0:N3}" -f $_.median_cpu_pct_total_capacity }},
        @{Name = "p95_cpu_pct"; Expression = { "{0:N3}" -f $_.p95_cpu_pct_total_capacity }},
        @{Name = "max_cpu_pct"; Expression = { "{0:N3}" -f $_.max_cpu_pct_total_capacity }} |
    Format-Table -AutoSize

Write-Host "Wrote:"
Write-Host "  $summaryCsv"
Write-Host "  $summaryJson"
Write-Host "  $rawCsv"
Write-Host "  $rawJson"
Write-Host "  $markerJson"
