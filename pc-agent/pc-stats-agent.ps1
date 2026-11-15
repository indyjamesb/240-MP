# Serves recent frame times for 240-MP's PC Stats screen.
# LibreHardwareMonitor is polled by the Pi directly. This process only runs
# PresentMon against the foreground game and publishes /stats.json.
#
# Windows PowerShell 5.1. Run elevated so PresentMon can open an ETW session
# and HttpListener can bind http://+:port/. See README.md. Fix PC Stats task.cmd
# and -Install register the hidden logon and resume-from-sleep task.

param(
    [switch]$Install
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ScriptPath = $PSCommandPath
$ConfigPath = Join-Path $ScriptDir "pc-stats.config.json"

function Get-AgentConfig {
    $cfg = @{
        listen_port   = 8086
        presentmon_path = ""
        sample_window = 600
        graph_points  = 160
    }
    if (Test-Path $ConfigPath) {
        $file = Get-Content -Raw -Path $ConfigPath | ConvertFrom-Json
        if ($file.listen_port) { $cfg.listen_port = [int]$file.listen_port }
        if ($file.presentmon_path) { $cfg.presentmon_path = [string]$file.presentmon_path }
        if ($file.sample_window) { $cfg.sample_window = [int]$file.sample_window }
        if ($file.graph_points) { $cfg.graph_points = [int]$file.graph_points }
    }
    return $cfg
}

function Find-PresentMon([hashtable]$cfg) {
    if ($cfg.presentmon_path -and (Test-Path -LiteralPath $cfg.presentmon_path)) {
        return (Resolve-Path -LiteralPath $cfg.presentmon_path).Path
    }
    $beside = @(Get-ChildItem -LiteralPath $ScriptDir -Filter "PresentMon*.exe" -ErrorAction SilentlyContinue)
    if ($beside.Count -gt 0) { return $beside[0].FullName }
    $candidates = @(
        (Join-Path $env:ProgramFiles "PresentMon\PresentMon.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "PresentMon\PresentMon.exe")
    )
    foreach ($path in $candidates) {
        if ($path -and (Test-Path -LiteralPath $path)) { return $path }
    }
    $cmd = Get-Command "PresentMon.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$LogPath = Join-Path $ScriptDir "pc-stats-agent.log"

function Write-AgentLog([string]$msg) {
    $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
    Write-Host $line
    try { Add-Content -LiteralPath $LogPath -Value $line -ErrorAction SilentlyContinue } catch {}
}

function ConvertTo-XmlText([string]$text) {
    if ($null -eq $text) { return '' }
    $out = $text.Replace('&', '&amp;')
    $out = $out.Replace('<', '&lt;')
    $out = $out.Replace('>', '&gt;')
    $out = $out.Replace('"', '&quot;')
    return $out
}

$PresentMonPidFile = Join-Path $ScriptDir 'pc-stats-presentmon.pid'

function Get-ParentProcessId {
    try {
        $me = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $PID" -ErrorAction Stop
        return [int]$me.ParentProcessId
    } catch {
        return 0
    }
}

# Only a process whose command line actually contains this script. Never the
# installer itself, never its parent (an SSH window that launched it), and
# never every powershell.exe on the machine.
function Stop-OwnedAgentProcesses {
    $parent = Get-ParentProcessId
    $procs = @()
    try {
        $procs = @(Get-CimInstance -ClassName Win32_Process -Filter "CommandLine LIKE '%pc-stats-agent.ps1%'" -ErrorAction Stop)
    } catch {
        return
    }
    foreach ($proc in $procs) {
        $id = [int]$proc.ProcessId
        if ($id -eq 0 -or $id -eq $PID -or $id -eq $parent) { continue }
        $cmd = [string]$proc.CommandLine
        if ($cmd -notlike '*pc-stats-agent.ps1*') { continue }
        Write-Host "Stopping previous agent pid $id"
        Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    }
}

# The PresentMon we started writes its PID. Stop that PID only if its command
# line is still our session, then drop the ETW session by name.
function Stop-RecordedPresentMon([string]$presentMonPath) {
    $parent = Get-ParentProcessId
    if (Test-Path -LiteralPath $PresentMonPidFile) {
        $owned = 0
        $raw = Get-Content -LiteralPath $PresentMonPidFile -ErrorAction SilentlyContinue | Select-Object -First 1
        [void][int]::TryParse(([string]$raw).Trim(), [ref]$owned)
        if ($owned -gt 4 -and $owned -ne $PID -and $owned -ne $parent) {
            $proc = $null
            try {
                $proc = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $owned" -ErrorAction Stop
            } catch {}
            $cmd = [string]$proc.CommandLine
            if ($cmd -like '*240mp-pc-stats*') {
                Write-Host "Stopping owned PresentMon pid $owned"
                Stop-Process -Id $owned -Force -ErrorAction SilentlyContinue
            }
        }
        Remove-Item -LiteralPath $PresentMonPidFile -Force -ErrorAction SilentlyContinue
    }
    if (-not $presentMonPath) { return }
    $stop = Start-Process -FilePath $presentMonPath -ArgumentList '--terminate_existing_session','--session_name','240mp-pc-stats' -WindowStyle Hidden -PassThru
    if (-not $stop.WaitForExit(5000)) {
        try { $stop.Kill() } catch {}
    }
}

function Stop-InstallTarget([string]$taskName) {
    $existing = $null
    try { $existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue } catch {}
    if ($existing) {
        try { Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue } catch {}
    }
    Stop-OwnedAgentProcesses
    Stop-RecordedPresentMon (Find-PresentMon (Get-AgentConfig))
}

function Install-Agent([hashtable]$cfg) {
    $port = $cfg.listen_port
    Write-Host "Opening TCP $port in the firewall..."
    & netsh advfirewall firewall delete rule name="240-MP PC Stats" | Out-Null
    & netsh advfirewall firewall add rule name="240-MP PC Stats" dir=in action=allow protocol=TCP localport=$port | Out-Host
    Write-Host "Reserving http://+:$port/ for this account..."
    & netsh http delete urlacl url="http://+:$port/" | Out-Null
    & netsh http add urlacl url="http://+:$port/" user="Everyone" | Out-Host

    $task = '240-MP PC Stats'
    Stop-InstallTarget $task

    $user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    $quotedDir = $ScriptDir.Replace("'", "''")
    $quotedScript = $ScriptPath.Replace("'", "''")
    $quotedLog = $LogPath.Replace("'", "''")
    # The call operator must be escaped when this string is placed in XML.
    $psCommand = "Set-Location -LiteralPath '$quotedDir'; & '$quotedScript' *>> '$quotedLog'"
    $arguments = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -Command `"$psCommand`""

    # PowerShell 5.1's New-ScheduledTaskSettingsSet enum has no StopExisting
    # (only Parallel, Queue, IgnoreNew). The task XML uses the schema value.
    $subscription = @(
        '<QueryList>'
        '  <Query Id="0" Path="System">'
        '    <Select Path="System">*[System[Provider[@Name=''Microsoft-Windows-Power-Troubleshooter''] and (EventID=1)]]</Select>'
        '  </Query>'
        '</QueryList>'
    ) -join "`n"

    $taskXml = @"
<?xml version="1.0"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>240-MP PC Stats frame-time agent</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>USERID</UserId>
    </LogonTrigger>
    <EventTrigger>
      <Enabled>true</Enabled>
      <Subscription>SUBSCRIPTION</Subscription>
      <Delay>PT15S</Delay>
    </EventTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>USERID</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>StopExisting</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>true</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>true</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <WakeToRun>false</WakeToRun>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>7</Priority>
    <RestartOnFailure>
      <Interval>PT1M</Interval>
      <Count>3</Count>
    </RestartOnFailure>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>powershell.exe</Command>
      <Arguments>ARGUMENTS</Arguments>
      <WorkingDirectory>WORKDIR</WorkingDirectory>
    </Exec>
  </Actions>
</Task>
"@
    $taskXml = $taskXml.Replace('USERID', (ConvertTo-XmlText $user))
    $taskXml = $taskXml.Replace('SUBSCRIPTION', (ConvertTo-XmlText $subscription))
    $taskXml = $taskXml.Replace('ARGUMENTS', (ConvertTo-XmlText $arguments))
    $taskXml = $taskXml.Replace('WORKDIR', (ConvertTo-XmlText $ScriptDir))

    try {
        $null = [xml]$taskXml
    } catch {
        Write-Host "The scheduled task XML is not well formed, so nothing was registered." -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }

    Write-Host "Registering '$task'..."
    try {
        Register-ScheduledTask -TaskName $task -Xml $taskXml -Force | Out-Null
    } catch {
        Write-Host "Failed to register scheduled task '$task'. The agent was not started." -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }

    try {
        Start-ScheduledTask -TaskName $task
    } catch {
        Write-Host "The task was registered but could not be started. It was not launched in this window." -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "Installed and started. '$task' is running hidden for $user."
    Write-Host "It also runs at logon, and 15 seconds after the PC resumes from sleep."
    Write-Host "A new start stops the previous instance. Output is appended to:"
    Write-Host "  $LogPath"
    Write-Host "There is no time limit, and a failed start is retried 3 times, a minute apart."
    Write-Host "LibreHardwareMonitor still needs its own Remote Web Server on port 8085."
}

$Config = Get-AgentConfig
if ($Install) {
    Install-Agent $Config
    exit 0
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class WinForeground {
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
}
"@

# Not games. The foreground window is one of these whenever you are on the desktop.
$Ignore = @{
    "applicationframehost" = $true
    "cmd" = $true
    "conhost" = $true
    "csrss" = $true
    "ctfmon" = $true
    "dwm" = $true
    "explorer" = $true
    "gamebar" = $true
    "gamebarftserver" = $true
    "idle" = $true
    "librehardwaremonitor" = $true
    "lockapp" = $true
    "nvcontainer" = $true
    "nvidia share" = $true
    "nvidia overlay" = $true
    "overlay" = $true
    "phoneexperiencehost" = $true
    "powershell" = $true
    "presentmon" = $true
    "pwsh" = $true
    "registry" = $true
    "searchhost" = $true
    "shellexperiencehost" = $true
    "sihost" = $true
    "startmenuexperiencehost" = $true
    "steam" = $true
    "steamwebhelper" = $true
    "system" = $true
    "systemsettings" = $true
    "taskmgr" = $true
    "textinputhost" = $true
    "widgets" = $true
    "windowsterminal" = $true
}

function Test-IgnoredName([string]$name) {
    if (-not $name) { return $true }
    $n = $name.ToLowerInvariant()
    if ($n.EndsWith(".exe")) { $n = $n.Substring(0, $n.Length - 4) }
    return $Ignore.ContainsKey($n)
}

function Get-ForegroundExe {
    $hwnd = [WinForeground]::GetForegroundWindow()
    if ($hwnd -eq [IntPtr]::Zero) { return $null }
    $procId = [uint32]0
    [void][WinForeground]::GetWindowThreadProcessId($hwnd, [ref]$procId)
    if ($procId -eq 0) { return $null }
    try {
        $proc = Get-Process -Id $procId -ErrorAction Stop
    } catch {
        return $null
    }
    if (Test-IgnoredName $proc.ProcessName) { return $null }
    return ($proc.ProcessName + ".exe")
}

function Format-Invariant([double]$n) {
    return [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0:0.###}", $n)
}

function ConvertTo-StatsJson($processName, $samples, $errorText) {
    if ($errorText) {
        $safe = ($errorText -replace '\\', '\\\\') -replace '"', '\"'
        return "{`"ok`":false,`"error`":`"$safe`",`"process`":`"`",`"fps_avg`":null,`"fps_1pct_low`":null,`"frame_ms`":[]}"
    }
    $safeName = ""
    if ($processName) {
        $safeName = ($processName -replace '\\', '\\\\') -replace '"', '\"'
    }
    $avg = "null"
    $low = "null"
    $series = ""
    if ($samples -and $samples.Count -gt 0) {
        $sum = 0.0
        foreach ($ms in $samples) { $sum += $ms }
        $avgMs = $sum / $samples.Count
        $sorted = @($samples | Sort-Object -Descending)
        $worstCount = [Math]::Max(1, [int][Math]::Round($samples.Count * 0.01, [MidpointRounding]::AwayFromZero))
        $worstSum = 0.0
        for ($i = 0; $i -lt $worstCount; $i++) { $worstSum += $sorted[$i] }
        $avg = Format-Invariant (1000.0 / $avgMs)
        $low = Format-Invariant (1000.0 / ($worstSum / $worstCount))
        $take = [Math]::Min($Config.graph_points, $samples.Count)
        $start = $samples.Count - $take
        $parts = @()
        for ($i = $start; $i -lt $samples.Count; $i++) { $parts += (Format-Invariant $samples[$i]) }
        $series = $parts -join ","
    }
    return "{`"ok`":true,`"process`":`"$safeName`",`"fps_avg`":$avg,`"fps_1pct_low`":$low,`"frame_ms`":[$series]}"
}

$PresentMon = Find-PresentMon $Config
$Shared = [hashtable]::Synchronized(@{
    running      = $true
    json         = (ConvertTo-StatsJson $null $null $(if (-not $PresentMon) { "PresentMon.exe not found next to this script" } else { $null }))
    listener     = $null
    gate         = (New-Object System.Object)
    chains       = (New-Object "System.Collections.Generic.Dictionary[string,System.Collections.Generic.List[double]]")
    column       = -1
    chainColumn  = -1
    lastFrameUtc = [DateTime]::MinValue
    maxSamples   = $Config.sample_window
    stderr       = ""
    captureError = $null
    stopping     = $false
})

$script:proc = $null
$script:subscriptions = @()
$script:capturing = $null
$script:lastStart = [DateTime]::MinValue

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Clear-Frames {
    [System.Threading.Monitor]::Enter($Shared.gate)
    try {
        $Shared.chains = New-Object "System.Collections.Generic.Dictionary[string,System.Collections.Generic.List[double]]"
        $Shared.column = -1
        $Shared.chainColumn = -1
        $Shared.lastFrameUtc = [DateTime]::MinValue
    } finally {
        [System.Threading.Monitor]::Exit($Shared.gate)
    }
}

function Stop-Capture {
    foreach ($sub in @($script:subscriptions)) {
        if (-not $sub) { continue }
        Unregister-Event -SourceIdentifier $sub.Name -ErrorAction SilentlyContinue
        Remove-Job -Name $sub.Name -Force -ErrorAction SilentlyContinue
    }
    $script:subscriptions = @()
    if ($script:proc -and -not $script:proc.HasExited) {
        $Shared.stopping = $true
        try { $script:proc.Kill() } catch {}
        # WaitForExit returns True and would print it into the log.
        try { [void]$script:proc.WaitForExit(2000) } catch {}
        $Shared.stopping = $false
    }
    $script:proc = $null
    $script:capturing = $null
    Remove-Item -LiteralPath $PresentMonPidFile -Force -ErrorAction SilentlyContinue
    $Shared.captureError = $null
    $Shared.stderr = ""
    Clear-Frames
}

function Note-CaptureFailure([string]$text) {
    $Shared.captureError = $text
    $Shared.json = ConvertTo-StatsJson $null $null $text
    Write-AgentLog $text
}

function Start-Capture([string]$exe) {
    if (-not $PresentMon) { return }
    # A crash-restart of the same game is throttled. Switching games is not.
    if ($exe -eq $script:capturing -and ((Get-Date) - $script:lastStart).TotalSeconds -lt 3) { return }
    Stop-Capture
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $PresentMon
    # --v1_metrics: PresentMon 2.6 does not emit MsBetweenPresents unless asked.
    # The reader still accepts FrameTime / MsBetweenAppStart if a build ignores the flag.
    $psi.Arguments = "--process_name `"$exe`" --output_stdout --no_console_stats --v1_metrics --stop_existing_session --session_name 240mp-pc-stats --exclude_dropped --terminate_on_proc_exit"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $proc.EnableRaisingEvents = $true
    try {
        [void]$proc.Start()
    } catch {
        $script:capturing = $exe
        $script:lastStart = Get-Date
        Note-CaptureFailure $_.Exception.Message
        return
    }
    $script:proc = $proc
    $script:capturing = $exe
    $script:lastStart = Get-Date
    try { Set-Content -LiteralPath $PresentMonPidFile -Value $proc.Id -Encoding Ascii } catch {}

    if ($proc.HasExited) {
        $err = ""
        try { $err = $proc.StandardError.ReadToEnd() } catch {}
        $code = $proc.ExitCode
        if ($code -ne 0) {
            $detail = ([string]$err).Trim()
            if ($detail.Length -gt 300) { $detail = $detail.Substring(0, 300) }
            $detail = ($detail -replace '\s+', ' ').Trim()
            $text = "PresentMon exited $code"
            if ($detail) { $text = "$text`: $detail" }
            Note-CaptureFailure $text
        }
        return
    }

    $script:subscriptions += Register-ObjectEvent -InputObject $proc -EventName OutputDataReceived -MessageData $Shared -Action {
        $s = $Event.MessageData
        $line = $Event.SourceEventArgs.Data
        if (-not $line) { return }
        $lower = $line.ToLowerInvariant()
        if ($lower.StartsWith("application,") -or $lower.StartsWith("application ")) {
            $headers = $line.Split(",")
            $index = @{}
            $chain = -1
            for ($i = 0; $i -lt $headers.Length; $i++) {
                $h = $headers[$i].Trim().Trim('"').ToLowerInvariant()
                if (-not $index.ContainsKey($h)) { $index[$h] = $i }
                if ($h -eq "swapchainaddress") { $chain = $i }
            }
            $col = -1
            foreach ($name in @("msbetweenpresents", "frametime", "msbetweenappstart", "msbetweendisplaychange")) {
                if ($index.ContainsKey($name)) { $col = [int]$index[$name]; break }
            }
            [System.Threading.Monitor]::Enter($s.gate)
            try {
                $s.column = $col
                $s.chainColumn = $chain
            } finally {
                [System.Threading.Monitor]::Exit($s.gate)
            }
            return
        }
        [System.Threading.Monitor]::Enter($s.gate)
        try {
            if ($s.column -lt 0) { return }
            $fields = $line.Split(",")
            if ($fields.Length -le $s.column) { return }
            $raw = $fields[$s.column].Trim().Trim('"')
            if ($raw.Length -eq 0 -or $raw -eq "NA") { return }
            $ms = 0.0
            if (-not [double]::TryParse($raw, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$ms)) { return }
            if ($ms -le 0 -or $ms -ge 5000) { return }
            $chainId = "default"
            if ($s.chainColumn -ge 0 -and $fields.Length -gt $s.chainColumn) {
                $chainId = $fields[$s.chainColumn].Trim()
            }
            if (-not $s.chains.ContainsKey($chainId)) {
                $s.chains[$chainId] = New-Object System.Collections.Generic.List[double]
            }
            $list = $s.chains[$chainId]
            $list.Add($ms)
            while ($list.Count -gt $s.maxSamples) { $list.RemoveAt(0) }
            $s.lastFrameUtc = [DateTime]::UtcNow
        } finally {
            [System.Threading.Monitor]::Exit($s.gate)
        }
    }
    $script:subscriptions += Register-ObjectEvent -InputObject $proc -EventName ErrorDataReceived -MessageData $Shared -Action {
        $s = $Event.MessageData
        $line = $Event.SourceEventArgs.Data
        if (-not $line) { return }
        $text = [string]$s.stderr
        if ($text.Length -lt 800) { $s.stderr = ($text + " " + $line).Trim() }
    }
    $script:subscriptions += Register-ObjectEvent -InputObject $proc -EventName Exited -MessageData $Shared -Action {
        $s = $Event.MessageData
        if ($s.stopping) { return }
        $code = 0
        try { $code = $Event.Sender.ExitCode } catch { return }
        if ($code -eq 0) { return }
        $detail = ([string]$s.stderr).Trim()
        if ($detail.Length -gt 300) { $detail = $detail.Substring(0, 300) }
        $detail = ($detail -replace '\s+', ' ').Trim()
        $text = "PresentMon exited $code"
        if ($detail) { $text = "$text`: $detail" }
        $s.captureError = $text
    }
    $proc.BeginOutputReadLine()
    $proc.BeginErrorReadLine()
}

function Get-ActiveSamples {
    $copy = $null
    $stale = $true
    [System.Threading.Monitor]::Enter($Shared.gate)
    try {
        $best = $null
        foreach ($key in @($Shared.chains.Keys)) {
            $list = $Shared.chains[$key]
            if (-not $best -or $list.Count -gt $best.Count) { $best = $list }
        }
        if ($best -and $best.Count -gt 0) {
            # CopyTo raced with Add on the event thread and threw
            # "Destination array was not long enough". ToArray snapshots first.
            $copy = $best.ToArray()
        }
        if ($Shared.lastFrameUtc -ne [DateTime]::MinValue) {
            $stale = ([DateTime]::UtcNow - $Shared.lastFrameUtc).TotalSeconds -gt 3
        }
    } finally {
        [System.Threading.Monitor]::Exit($Shared.gate)
    }
    if ($stale) { return $null }
    # The comma keeps PowerShell from unwrapping a one-element array into a scalar.
    return ,$copy
}

function Publish-Stats([string]$exe) {
    if (-not $PresentMon) {
        $Shared.json = ConvertTo-StatsJson $null $null "PresentMon.exe not found next to this script"
        return
    }
    if ($Shared.captureError) {
        $Shared.json = ConvertTo-StatsJson $null $null ([string]$Shared.captureError)
        return
    }
    $samples = $null
    if ($exe) { $samples = Get-ActiveSamples }
    $Shared.json = ConvertTo-StatsJson $exe $samples $null
}

$listenerScript = {
    param($shared, $port)
    $listener = New-Object System.Net.HttpListener
    $listener.Prefixes.Add("http://+:$port/")
    try {
        $listener.Start()
    } catch {
        $shared.json = "{`"ok`":false,`"error`":`"cannot bind port $port (run -Install from an elevated prompt)`",`"process`":`"`",`"fps_avg`":null,`"fps_1pct_low`":null,`"frame_ms`":[]}"
        $shared.running = $false
        return
    }
    $shared.listener = $listener
    while ($shared.running) {
        try {
            $ctx = $listener.GetContext()
        } catch {
            break
        }
        try {
            $body = [Text.Encoding]::UTF8.GetBytes([string]$shared.json)
            $ctx.Response.ContentType = "application/json"
            $ctx.Response.StatusCode = 200
            $ctx.Response.Headers.Add("Cache-Control", "no-store")
            $ctx.Response.ContentLength64 = $body.Length
            $ctx.Response.OutputStream.Write($body, 0, $body.Length)
        } catch {
        } finally {
            try { $ctx.Response.Close() } catch {}
        }
    }
    try { $listener.Stop() } catch {}
    try { $listener.Close() } catch {}
}

function Release-AgentPort([int]$port) {
    # Only if the listener is a previous copy of this script. Never a PID
    # chosen by process name, and never this window or the one that launched it.
    $parent = Get-ParentProcessId
    $owners = New-Object System.Collections.Generic.List[int]
    & netstat -ano | ForEach-Object {
        if ($_ -match (":$port\s+\S+\s+LISTENING\s+(\d+)\s*$")) {
            [void]$owners.Add([int]$Matches[1])
        }
    }
    foreach ($owner in ($owners | Select-Object -Unique)) {
        if ($owner -le 4 -or $owner -eq $PID -or $owner -eq $parent) { continue }
        $proc = $null
        try {
            $proc = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $owner" -ErrorAction Stop
        } catch {}
        $cmd = [string]$proc.CommandLine
        if ($cmd -notlike '*pc-stats-agent.ps1*') { continue }
        Write-AgentLog "Releasing TCP $port held by previous agent pid $owner"
        Stop-Process -Id $owner -Force -ErrorAction SilentlyContinue
    }
}

Stop-OwnedAgentProcesses
Stop-RecordedPresentMon $PresentMon
Release-AgentPort $Config.listen_port
Start-Sleep -Seconds 1

$rs = [runspacefactory]::CreateRunspace()
$rs.Open()
$ps = [powershell]::Create()
$ps.Runspace = $rs
[void]$ps.AddScript($listenerScript).AddArgument($Shared).AddArgument($Config.listen_port)
$handle = $ps.BeginInvoke()

Write-AgentLog "PC Stats agent listening on http://+:$($Config.listen_port)/stats.json"
if ($PresentMon) { Write-AgentLog "PresentMon: $PresentMon" }
else { Write-AgentLog "PresentMon.exe was not found. Frame times will stay empty until it is." }
if (-not (Test-IsAdmin)) {
    Write-AgentLog "WARNING: not running as administrator. PresentMon ETW needs an elevated window (title bar 'Administrator:') or membership in Performance Log Users. Capture will fail with access denied until then."
}

Start-Sleep -Milliseconds 400
if (-not $Shared.running) {
    Write-Host "The listener did not start. Re-run -Install from an elevated PowerShell."
    try { $ps.EndInvoke($handle) } catch {}
    $ps.Dispose()
    $rs.Close()
    exit 1
}

try {
    $lastGame = $null
    $lastGameSeen = [DateTime]::MinValue
    while ($Shared.running) {
        try {
            $exe = Get-ForegroundExe
            if ($exe) {
                $lastGame = $exe
                $lastGameSeen = Get-Date
            } elseif ($lastGame -and ((Get-Date) - $lastGameSeen).TotalSeconds -lt 15) {
                # A short alt-tab should not drop the game's frame window.
                $exe = $lastGame
            } else {
                $exe = $null
                $lastGame = $null
            }

            $alive = $script:proc -and -not $script:proc.HasExited
            # A PresentMon that survived sleep often sits alive and silent.
            # No frames for 15s while a game is in front means relaunch it.
            $stalled = $false
            if ($alive -and $exe -and $exe -eq $script:capturing) {
                $startedAgo = ((Get-Date) - $script:lastStart).TotalSeconds
                $frameAge = 1000000
                if ($Shared.lastFrameUtc -ne [DateTime]::MinValue) {
                    $frameAge = ([DateTime]::UtcNow - $Shared.lastFrameUtc).TotalSeconds
                }
                if ($startedAgo -gt 15 -and $frameAge -gt 15) { $stalled = $true }
            }
            if (-not $exe) {
                if ($script:capturing) { Stop-Capture }
            } elseif ($exe -ne $script:capturing -or -not $alive -or $stalled) {
                if ($stalled) { Write-AgentLog "PresentMon stalled for $exe; relaunching" }
                Start-Capture $exe
            }
            Publish-Stats $exe
        } catch {
            Write-AgentLog ("loop error: " + $_.Exception.Message)
        }
        Start-Sleep -Milliseconds 250
    }
} finally {
    $Shared.running = $false
    if ($Shared.listener) {
        try { $Shared.listener.Stop() } catch {}
    }
    Stop-Capture
    try { $ps.EndInvoke($handle) } catch {}
    $ps.Dispose()
    $rs.Close()
}
