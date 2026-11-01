#==================================================================================
#
# コードの整形スクリプト [tidy.ps1]
#
# 1. clang-format（.clang-format の設定）で整形する
#    ・if / for / while の本体を {} で囲む（InsertBraces）
#    ・関数の定義の間を 1 行空ける（SeparateDefinitionBlocks）
# 2. その後、次の後処理を行う
#    ・本体が return / continue / break の 1 文だけの if は 1 行に戻す（early return）
#    ・処理のまとまりの間に空行を入れる（字下げしたコメントの前、ブロックの終わり "}" の後）
#
# 使い方:  powershell -ExecutionPolicy Bypass -File tools\tidy.ps1 [ファイル...]
#          ファイルを省略すると src / project/Scripts / tests / android のソースすべて。
# 注意: clang-format だけを実行し直すと 1 行の if にも {} が付くので、必ずこのスクリプトで整形する。
#
#==================================================================================
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Files)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$limit = 130 # 1 行の最大の桁数（.clang-format の ColumnLimit と同じ）

#----------------------------------------------------------------------------------
# clang-format の場所（PATH → Visual Studio に付属の物）
#----------------------------------------------------------------------------------
function Find-ClangFormat {
    $command = Get-Command clang-format.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        $candidate = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-format.exe'
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw 'clang-format.exe was not found (install "C++ Clang tools" in Visual Studio).'
}

#----------------------------------------------------------------------------------
# 対象のファイル（生成したシェーダーのバイトコードと third_party は除く）
#----------------------------------------------------------------------------------
if (-not $Files) {
    $Files = @()
    foreach ($folder in 'src', 'project\Scripts', 'tests', 'android\app\src\main\cpp') {
        $Files += Get-ChildItem (Join-Path $root $folder) -Recurse -Include *.cpp, *.h |
            Where-Object { $_.Name -notin 'OverlayVS.h', 'OverlayPS.h' } |
            ForEach-Object { $_.FullName }
    }
}

#----------------------------------------------------------------------------------
# 後処理: 1 行の early return に戻す
#----------------------------------------------------------------------------------
function Join-EarlyReturns([string[]]$lines) {
    $result = New-Object System.Collections.Generic.List[string]
    $i = 0
    while ($i -lt $lines.Count) {
        $line = $lines[$i]

        # if (条件) {  /  return ...; | continue; | break;  /  }  （else が続かない物）
        if ($i + 2 -lt $lines.Count -and $line -match '^(\s*)if \((.*)\) \{$') {
            $indent = $Matches[1]
            $body = $lines[$i + 1]
            $close = $lines[$i + 2]
            $next = if ($i + 3 -lt $lines.Count) { $lines[$i + 3] } else { '' }
            $isJump = $body -match ('^' + [regex]::Escape($indent) + '    ((return\b.*;|continue;|break;)(\s*//.*)?)$')
            $jump = $Matches[1]
            $isClose = $close -eq ($indent + '}')
            $hasElse = $next -match ('^' + [regex]::Escape($indent) + 'else\b')

            if ($isJump -and $isClose -and -not $hasElse) {
                $joined = $line.Substring(0, $line.Length - 2) + ' ' + $jump # 末尾の " {" を外して本体をつなげる
                if ($joined.Length -le $limit) {
                    $result.Add($joined)
                    $i += 3
                    continue
                }
            }
        }

        $result.Add($line)
        $i++
    }
    return , $result.ToArray()
}

#----------------------------------------------------------------------------------
# 後処理: 処理のまとまりの間に空行を入れる
#----------------------------------------------------------------------------------
function Add-BlankLines([string[]]$lines) {
    $result = New-Object System.Collections.Generic.List[string]
    $inRawString = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $previous = if ($result.Count -gt 0) { $result[$result.Count - 1] } else { '' }

        # 生文字列リテラル R"( ... )" の中は変更しない
        if ($inRawString) {
            $result.Add($line)
            if ($line.Contains(')"')) {
                $inRawString = $false
            }
            continue
        }

        $trimmed = $line.Trim()
        $previousTrimmed = $previous.Trim()
        $indent = $line.Length - $line.TrimStart().Length
        $previousIsCode = $previousTrimmed -ne '' -and -not $previousTrimmed.StartsWith('//') -and
                          -not $previousTrimmed.StartsWith('#') -and -not $previous.EndsWith('\')

        # 字下げしたコメントの前: 直前が文の終わり（; か }）なら空行を入れる
        $isIndentedComment = $indent -gt 0 -and $trimmed.StartsWith('//')
        if ($isIndentedComment -and $previousIsCode -and ($previousTrimmed.EndsWith(';') -or $previousTrimmed -eq '}')) {
            $result.Add('')
        }

        # ブロックの終わり "}" の後: 同じ字下げで次の文が続くなら空行を入れる
        $previousIndent = $previous.Length - $previous.TrimStart().Length
        $isCodeAfterBlock = $previousTrimmed -eq '}' -and $previousIndent -gt 0 -and $trimmed -ne '' -and
                            $indent -eq $previousIndent -and -not $trimmed.StartsWith('//') -and
                            $trimmed -notmatch '^(\}|\)|else\b|catch\b|case\b|default\b|while\b|#)'
        if ($isCodeAfterBlock) {
            $result.Add('')
        }

        $result.Add($line)

        # R"( で始まり、同じ行で閉じていない生文字列リテラル
        $rawStart = $line.IndexOf('R"(')
        if ($rawStart -ge 0 -and $line.IndexOf(')"', $rawStart + 3) -lt 0) {
            $inRawString = $true
        }
    }
    return , $result.ToArray()
}

#----------------------------------------------------------------------------------
# 実行
#----------------------------------------------------------------------------------
$clangFormat = Find-ClangFormat
$utf8 = New-Object System.Text.UTF8Encoding $false

foreach ($file in $Files) {
    $path = (Resolve-Path $file).Path
    & $clangFormat -i -style=file $path
    if ($LASTEXITCODE -ne 0) {
        throw "clang-format failed: $path"
    }

    $text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
    $lines = $text.Split("`n")
    $lines = Join-EarlyReturns $lines
    $lines = Add-BlankLines $lines
    [IO.File]::WriteAllText($path, ($lines -join "`n"), $utf8)
    "tidied $path"
}
