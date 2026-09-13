#Requires -Version 7
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(0, 65535)][int]$Seed = 23117,
    [ValidateRange(1, 32)][int]$Programs = 12
)

$ErrorActionPreference = "Stop"
$state = $Seed
function Next-Choice([int]$Limit) {
    $script:state = ($script:state * 25173 + 13849) -band 65535
    return $script:state % $Limit
}
$samples = @(0, 1, 127, 255, 256, 32767, 32768, 65535)
$source = [System.Text.StringBuilder]::new()
[void]$source.AppendLine('#include <stdio.h>')
[void]$source.AppendLine('#ifndef FUZZ_MUTATE')
[void]$source.AppendLine('#define FUZZ_MUTATE 0')
[void]$source.AppendLine('#endif')
[void]$source.AppendLine('unsigned int touch8(unsigned char *target, unsigned char *alias, unsigned int value) { *target = (unsigned char)(*target ^ value); return *alias; }')
[void]$source.AppendLine('unsigned int touch16(unsigned int *target, unsigned int *alias, unsigned int value) { *target ^= value; return *alias; }')
[void]$source.AppendLine('unsigned int alter8(unsigned char *target, unsigned char *alias, unsigned int value) { *alias = (unsigned char)(*alias + value); return *target; }')
[void]$source.AppendLine('unsigned int alter16(unsigned int *target, unsigned int *alias, unsigned int value) { *alias += value; return *target; }')
$expected = [System.Collections.Generic.List[int]]::new()
for ($program = 0; $program -lt $Programs; ++$program) {
    $width = if (($program % 2) -eq 0) { 8 } else { 16 }
    $mask = if ($width -eq 8) { 255 } else { 65535 }
    $element = if ($width -eq 8) { "unsigned char" } else { "unsigned int" }
    $operations = @()
    for ($step = 0; $step -lt 6; ++$step) {
        $operations += @{ Kind = (Next-Choice 5); Constant = (1 + (Next-Choice 255)); Shift = (Next-Choice 8) }
    }
    [void]$source.AppendLine("unsigned int fuzz$program(unsigned int seed) {")
    [void]$source.AppendLine("$element data[4]; $element *alias; unsigned int first, second, saved, count; int slot;")
    [void]$source.AppendLine("unsigned int (*callback)($element *, $element *, unsigned int) = (seed & 64U) ? touch$width : alter$width;")
    [void]$source.AppendLine('for (slot = 0; slot < 4; ++slot) data[slot] = seed + (unsigned int)slot * 17U;')
    [void]$source.AppendLine('alias = (seed & 1U) ? &data[1] : &data[2]; first = seed; second = seed ^ 43690U; saved = data[1];')
    foreach ($operation in $operations) {
        $constant = $operation.Constant
        $expression = switch ($operation.Kind) {
            0 { "first + ${constant}U" }
            1 { "first ^ ${constant}U" }
            2 { "first * ${constant}U" }
            3 { "first >> $($operation.Shift)" }
            4 { "first / ${constant}U" }
        }
        [void]$source.AppendLine("first = (unsigned int)($expression);")
        [void]$source.AppendLine('second += callback(&data[1], alias, first);')
    }
    [void]$source.AppendLine('for (count = 0; count < (seed & 3U); ++count) { first += data[count]; second ^= first; }')
    [void]$source.AppendLine('if (seed & 128U) first ^= second; else first += second;')
    [void]$source.AppendLine('return (unsigned int)(first + saved + data[1] + data[2]) ^ FUZZ_MUTATE; }')
    foreach ($sample in $samples) {
        $data = @(0..3 | ForEach-Object { ($sample + $_ * 17) -band $mask })
        $aliasIndex = if (($sample -band 1) -ne 0) { 1 } else { 2 }
        [long]$first = $sample
        [long]$second = $sample -bxor 43690
        $saved = $data[1]
        foreach ($operation in $operations) {
            $first = switch ($operation.Kind) {
                0 { ($first + $operation.Constant) -band 65535 }
                1 { $first -bxor $operation.Constant }
                2 { ($first * $operation.Constant) -band 65535 }
                3 { $first -shr $operation.Shift }
                4 { [long][Math]::Floor($first / $operation.Constant) }
            }
            if (($sample -band 64) -ne 0) {
                $data[1] = ($data[1] -bxor $first) -band $mask
                $callbackResult = $data[$aliasIndex]
            } else {
                $data[$aliasIndex] =
                    ($data[$aliasIndex] + $first) -band $mask
                $callbackResult = $data[1]
            }
            $second = ($second + $callbackResult) -band 65535
        }
        for ($count = 0; $count -lt ($sample -band 3); ++$count) {
            $first = ($first + $data[$count]) -band 65535
            $second = $second -bxor $first
        }
        $first = if (($sample -band 128) -ne 0) { $first -bxor $second } else { ($first + $second) -band 65535 }
        $expected.Add(($first + $saved + $data[1] + $data[2]) -band 65535)
    }
}
[void]$source.AppendLine("static unsigned int inputs[8] = { $($samples -join ',') };")
[void]$source.AppendLine("static unsigned int expected[$($expected.Count)] = { $($expected -join ',') };")
[void]$source.AppendLine('static int failures, checks;')
[void]$source.AppendLine('static void check(unsigned int actual) { if (actual != expected[checks]) { ++failures; printf("FAIL fuzz check=%d got=%u expected=%u\n", checks, actual, expected[checks]); } ++checks; }')
[void]$source.AppendLine('int main(void) { int sample;')
for ($program = 0; $program -lt $Programs; ++$program) {
    [void]$source.AppendLine("for (sample = 0; sample < 8; ++sample) check(fuzz$program(inputs[sample]));")
}
[void]$source.AppendLine(('printf("MIR fuzz seed={0} checks=%d failures=%d\n", checks, failures); return failures != 0; }}' -f $Seed))
[System.IO.File]::WriteAllText($OutputPath, $source.ToString(), [System.Text.Encoding]::ASCII)