# Shared data loading, leaf accounting, and process isolation for the clobber runner.

function Import-MirClobberCases(
    [string]$Directory,
    [string]$RepoRoot,
    [string[]]$ExistingNames,
    [string[]]$ReservedNames = @()
) {
    $names = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($name in @($ExistingNames) + @($ReservedNames)) {
        [void]$names.Add($name)
    }
    $stringProperties = @(
        "Name", "Group", "ExactTemplate", "ExactFunction",
        "RequiredGenericFunction", "RequiredSelectorFunction",
        "RequiredSelector", "RequiredCandidate", "MachineMutation",
        "MachineMutationFunction"
    )
    $arrayProperties = @(
        "Sources", "FixturePaths", "Defines", "Expected", "Args",
        "AssemblyPatterns", "ForbiddenAssemblyPatterns", "DebugModes"
    )
    $boolProperties = @("RequireExact", "RequireRejected", "OddUpperRuntime")
    $allowed = $stringProperties + $arrayProperties + $boolProperties +
        @("Exit", "StackBytes", "StackModes")
    $definitions = [System.Collections.Generic.List[object]]::new()
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        return
    }
    $files = [string[]]@(Get-ChildItem -LiteralPath $Directory -Filter "*.json" -File |
        ForEach-Object { $_.FullName })
    [Array]::Sort($files, [System.StringComparer]::Ordinal)
    foreach ($file in $files) {
        $data = ConvertFrom-Json -InputObject (
            Get-Content -LiteralPath $file -Raw) -AsHashtable -NoEnumerate
        if ($data -isnot [array]) {
            throw "MIR clobber campaign must be a JSON array: $file"
        }
        foreach ($definition in $data) {
            if ($definition -isnot [System.Collections.IDictionary]) {
                throw "MIR clobber case must be an object: $file"
            }
            foreach ($property in $definition.Keys) {
                if ($property -cnotin $allowed) {
                    throw "Unknown MIR clobber case property '$property': $file"
                }
            }
            foreach ($property in @("Name", "Sources", "Expected", "Exit")) {
                if (-not $definition.Contains($property)) {
                    throw "Missing MIR clobber case property '$property': $file"
                }
            }
            foreach ($property in $stringProperties) {
                if ($definition.Contains($property) -and
                    $definition[$property] -isnot [string]) {
                    throw "MIR clobber $property must be a string: $file"
                }
            }
            foreach ($property in @("Name", "Group")) {
                if ($definition.Contains($property) -and
                    $definition[$property] -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$') {
                    throw "Invalid MIR clobber $property in $file"
                }
            }
            if (-not $names.Add($definition.Name)) {
                throw "Duplicate or reserved MIR clobber case name '$($definition.Name)': $file"
            }
            foreach ($property in $arrayProperties) {
                if (-not $definition.Contains($property)) { continue }
                if ($definition[$property] -isnot [array] -or
                    @($definition[$property] | Where-Object {
                        $_ -isnot [string]
                    }).Count) {
                    throw "MIR clobber $property must be an array of strings: $file"
                }
            }
            if ($definition.Sources.Count -eq 0) {
                throw "MIR clobber Sources must not be empty: $file"
            }
            foreach ($property in $boolProperties) {
                if ($definition.Contains($property) -and
                    $definition[$property] -isnot [bool]) {
                    throw "MIR clobber $property must be a boolean: $file"
                }
            }
            foreach ($property in @("Exit", "StackBytes")) {
                if (-not $definition.Contains($property)) { continue }
                $value = $definition[$property]
                if (($value -isnot [long] -and $value -isnot [int]) -or
                    $value -lt [int]::MinValue -or $value -gt [int]::MaxValue -or
                    ($property -eq "StackBytes" -and $value -le 0)) {
                    throw "Invalid MIR clobber $property in $file"
                }
            }
            if ($definition.Contains("StackModes")) {
                $modes = $definition.StackModes
                if ($modes -isnot [array] -or $modes.Count -eq 0 -or
                    @($modes | Where-Object { $_ -isnot [bool] }).Count -or
                    @($modes | Select-Object -Unique).Count -ne $modes.Count) {
                    throw "StackModes must contain distinct booleans: $file"
                }
            }
            if ($definition.Contains("DebugModes")) {
                $modes = $definition.DebugModes
                if (@($modes | Where-Object { $_ -cnotin @("true", "lines") }).Count -or
                    @($modes | Select-Object -Unique).Count -ne $modes.Count) {
                    throw "DebugModes must contain distinct 'true' or 'lines' values: $file"
                }
            }
            if (($definition.RequireExact -or $definition.RequireRejected) -and
                (-not $definition.ExactTemplate -or -not $definition.ExactFunction)) {
                throw "Exact assertions require ExactTemplate and ExactFunction: $file"
            }
            if ($definition.RequireExact -and $definition.RequireRejected) {
                throw "RequireExact and RequireRejected cannot both be true: $file"
            }
            if (($definition.RequiredSelector -or $definition.RequiredSelectorFunction -or
                    $definition.RequiredCandidate) -and
                (-not $definition.RequiredSelector -or -not $definition.RequiredSelectorFunction)) {
                throw "Selector assertions require RequiredSelector and RequiredSelectorFunction: $file"
            }
            if ($definition.MachineMutation -and -not $definition.MachineMutationFunction) {
                throw "MachineMutation requires MachineMutationFunction: $file"
            }
            foreach ($property in @("Sources", "FixturePaths")) {
                if (-not $definition.Contains($property)) { continue }
                $definition[$property] = @($definition[$property] | ForEach-Object {
                    if ([string]::IsNullOrWhiteSpace($_) -or
                        [System.IO.Path]::IsPathRooted($_)) {
                        throw "$property must use repo-relative paths: $file"
                    }
                    $path = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $_))
                    $relative = [System.IO.Path]::GetRelativePath($RepoRoot, $path)
                    if ($relative -eq ".." -or $relative.StartsWith("../") -or
                        $relative.StartsWith("..\") -or
                        -not (Test-Path -LiteralPath $path -PathType Leaf)) {
                        throw "Invalid repo-relative $property path '$_': $file"
                    }
                    $path
                })
            }
            $definitions.Add([pscustomobject]$definition)
        }
    }
    foreach ($definition in $definitions) {
        if ($definition.Group -and
            $definition.Group -in (@($ExistingNames) + @($definitions.Name))) {
            throw "MIR clobber Group collides with case name '$($definition.Group)'"
        }
        $definition
    }
}

function Add-MirClobberExecution(
    $Plan, [string]$Key, [string]$Command,
    [System.Collections.IDictionary]$Parameters, [string[]]$EnvironmentNames
) {
    $arguments = @{}
    foreach ($name in $Parameters.Keys) {
        $arguments[$name] = $Parameters[$name]
    }
    $environment = @{}
    foreach ($name in $EnvironmentNames) {
        $environment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
    }
    if ($Plan.ContainsKey($Key)) {
        throw "duplicate MIR clobber inventory key: $Key"
    }
    $Plan.Add($Key, [pscustomobject]@{
        Key = $Key; Command = $Command; Parameters = $arguments
        Environment = $environment
    })
}

function Get-MirClobberShard([string[]]$Keys, [int]$Index, [int]$Count) {
    if ($Count -lt 1 -or $Index -lt 0 -or $Index -ge $Count) {
        throw "Invalid MIR clobber shard $Index/$Count"
    }
    $ordered = [string[]]@($Keys)
    [Array]::Sort($ordered, [System.StringComparer]::Ordinal)
    for ($i = $Index; $i -lt $ordered.Count; $i += $Count) {
        $ordered[$i]
    }
}

function Assert-MirClobberManifest([string[]]$Expected, [string[]]$Actual) {
    $expectedSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    $actualSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    foreach ($key in $Expected) {
        if (-not $expectedSet.Add($key)) {
            throw "duplicate MIR clobber expected key: $key"
        }
    }
    foreach ($key in $Actual) {
        if (-not $actualSet.Add($key)) {
            throw "duplicate MIR clobber execution: $key"
        }
        if (-not $expectedSet.Contains($key)) {
            throw "unexpected MIR clobber execution: $key"
        }
    }
    foreach ($key in $Expected) {
        if (-not $actualSet.Contains($key)) {
            throw "missing MIR clobber execution: $key"
        }
    }
}

function Write-MirClobberManifest([string]$Path, [string[]]$Keys, [string]$RepoRoot) {
    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $RepoRoot $Path
    }
    $Path = [System.IO.Path]::GetFullPath($Path)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    $ordered = [string[]]@($Keys)
    [Array]::Sort($ordered, [System.StringComparer]::Ordinal)
    ConvertTo-Json -InputObject $ordered |
        Set-Content -LiteralPath $Path -Encoding utf8
}

function Invoke-MirClobberShards(
    [string]$ScriptPath, [hashtable]$Parameters, [int]$Jobs,
    [string[]]$Expected, [string]$WorkRoot, [string]$RepoRoot,
    [hashtable]$Environment
) {
    $children = [System.Collections.Generic.List[object]]::new()
    $actual = [System.Collections.Generic.List[string]]::new()
    try {
        for ($index = 0; $index -lt $Jobs; ++$index) {
            $manifest = Join-Path $WorkRoot "shard-$index.json"
            $parameterPath = Join-Path $WorkRoot "parameters-$index.json"
            $childParameters = $Parameters.Clone()
            $childParameters.ShardIndex = $index
            $childParameters.ShardCount = $Jobs
            $childParameters.ExecutionManifest = $manifest
            ConvertTo-Json -InputObject $childParameters -Depth 10 |
                Set-Content -LiteralPath $parameterPath -Encoding utf8
            $command = "`$parameters = Get-Content -LiteralPath '" +
                $parameterPath.Replace("'", "''") +
                "' -Raw | ConvertFrom-Json -AsHashtable; & '" +
                $ScriptPath.Replace("'", "''") + "' @parameters"
            $start = [System.Diagnostics.ProcessStartInfo]::new()
            $start.FileName = Join-Path $PSHOME $(if ($IsWindows) { "pwsh.exe" } else { "pwsh" })
            $start.WorkingDirectory = $RepoRoot
            $start.UseShellExecute = $false
            $start.RedirectStandardOutput = $true
            $start.RedirectStandardError = $true
            foreach ($argument in @("-NoProfile", "-NonInteractive", "-EncodedCommand",
                [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command)))) {
                $start.ArgumentList.Add($argument)
            }
            foreach ($name in $Environment.Keys) {
                if ($null -eq $Environment[$name]) {
                    [void]$start.Environment.Remove($name)
                } else {
                    $start.Environment[$name] = $Environment[$name]
                }
            }
            $profile = $start.Environment["LLVM_PROFILE_FILE"]
            if ($profile) {
                $profileName = [System.IO.Path]::GetFileNameWithoutExtension($profile) +
                    "-clobber-$PID-$index" + [System.IO.Path]::GetExtension($profile)
                $start.Environment["LLVM_PROFILE_FILE"] = [System.IO.Path]::Combine(
                    [System.IO.Path]::GetDirectoryName($profile), $profileName)
            }
            $process = [System.Diagnostics.Process]::new()
            $process.StartInfo = $start
            if (-not $process.Start()) { throw "Failed to start MIR clobber shard $index" }
            $children.Add([pscustomobject]@{
                Process = $process; Index = $index; Manifest = $manifest
                Stdout = $process.StandardOutput.ReadToEndAsync()
                Stderr = $process.StandardError.ReadToEndAsync()
            })
        }
        $pending = @($children)
        while ($pending.Count) {
            foreach ($child in @($pending)) {
                if (-not $child.Process.HasExited) { continue }
                $child.Process.WaitForExit()
                $output = $child.Stdout.GetAwaiter().GetResult() +
                    $child.Stderr.GetAwaiter().GetResult()
                Set-Content -LiteralPath (Join-Path $WorkRoot "shard-$($child.Index).log") `
                    -Value $output -Encoding utf8
                if ($child.Process.ExitCode -ne 0) {
                    throw "MIR clobber shard $($child.Index) failed:`n$output"
                }
                $keys = ConvertFrom-Json -InputObject (
                    Get-Content -LiteralPath $child.Manifest -Raw) -NoEnumerate
                if ($keys -isnot [array] -or
                    @($keys | Where-Object { $_ -isnot [string] }).Count) {
                    throw "Invalid MIR clobber shard manifest: $($child.Manifest)"
                }
                Assert-MirClobberManifest `
                    @(Get-MirClobberShard $Expected $child.Index $Jobs) $keys
                $actual.AddRange([string[]]$keys)
                $pending = @($pending | Where-Object { $_.Index -ne $child.Index })
            }
            if ($pending.Count) { Start-Sleep -Milliseconds 100 }
        }
        Assert-MirClobberManifest $Expected $actual.ToArray()
        $actual.ToArray()
    } finally {
        foreach ($child in $children) {
            if (-not $child.Process.HasExited) {
                $child.Process.Kill($true)
                $child.Process.WaitForExit()
            }
            $child.Process.Dispose()
        }
    }
}
