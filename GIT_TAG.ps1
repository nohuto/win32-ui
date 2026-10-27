$ErrorActionPreference = 'Stop'
$tag = git -C $PSScriptRoot rev-parse HEAD
foreach ($app in 'thread-sim', 'regkit') {
    $repo = Join-Path (Split-Path $PSScriptRoot) $app
    $file = "$repo\CMakeLists.txt"
    $text = [IO.File]::ReadAllText($file)
    $new = $text -replace '(GIT_TAG\s+)[0-9a-f]{40}', "`${1}$tag"
    if ($new -eq $text) { continue }
    [IO.File]::WriteAllText($file, $new)
    git -C $repo commit -q -m "win32-ui $($tag.Substring(0, 7))" -- CMakeLists.txt
    git -C $repo push -q
}
