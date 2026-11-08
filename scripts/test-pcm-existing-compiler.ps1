# Uses the Roslyn libraries already shipped with PowerShell 7; installs nothing.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -Path (Join-Path $PSHOME 'Microsoft.CodeAnalysis.dll')
Add-Type -Path (Join-Path $PSHOME 'Microsoft.CodeAnalysis.CSharp.dll')
[xml]$project = Get-Content (Join-Path $root 'tests\ApexSenseBridgeTray.LearningTests.csproj')
$ns = New-Object System.Xml.XmlNamespaceManager($project.NameTable)
$ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$trees = [System.Collections.Generic.List[Microsoft.CodeAnalysis.SyntaxTree]]::new()
foreach ($node in $project.SelectNodes('//m:Compile', $ns)) {
    $path = [IO.Path]::GetFullPath((Join-Path "$root\tests" $node.Include))
    $trees.Add([Microsoft.CodeAnalysis.CSharp.CSharpSyntaxTree]::ParseText([IO.File]::ReadAllText($path), $null, $path))
}
$references = [System.Collections.Generic.List[Microsoft.CodeAnalysis.MetadataReference]]::new()
$framework = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319'
$references.Add([Microsoft.CodeAnalysis.MetadataReference]::CreateFromFile("$framework\mscorlib.dll"))
foreach ($node in $project.SelectNodes('//m:Reference', $ns)) {
    $name = $node.Include + '.dll'
    $path = Join-Path $framework $name
    if (-not (Test-Path $path)) {
        $path = (Get-ChildItem "C:\Windows\Microsoft.NET\assembly\GAC_MSIL\$($node.Include)" -Filter $name -Recurse | Select-Object -First 1).FullName
    }
    $references.Add([Microsoft.CodeAnalysis.MetadataReference]::CreateFromFile($path))
}
$options = [Microsoft.CodeAnalysis.CSharp.CSharpCompilationOptions]::new([Microsoft.CodeAnalysis.OutputKind]::ConsoleApplication)
$compilation = [Microsoft.CodeAnalysis.CSharp.CSharpCompilation]::Create('ApexSenseBridgeTray.LearningTests', $trees, $references, $options)
$outDir = Join-Path $root 'tests\bin\PcmVerification'
New-Item -ItemType Directory -Force $outDir | Out-Null
$stream = [IO.File]::Create((Join-Path $outDir 'ApexSenseBridgeTray.LearningTests.exe'))
$resource = [Microsoft.CodeAnalysis.ResourceDescription]::new('ApexSenseBridgeTray.supported_games.json', [Func[IO.Stream]]{ [IO.File]::OpenRead((Join-Path $root 'data\supported_games.json')) }, $true)
$result = $compilation.Emit($stream, $null, $null, $null, [Microsoft.CodeAnalysis.ResourceDescription[]]@($resource))
$stream.Dispose()
$result.Diagnostics | ForEach-Object { $_.ToString() }
if (-not $result.Success) { throw 'Actual C# production/test compilation failed.' }
Write-Host 'Actual C# learning-test compilation passed (existing Roslyn + Framework runtime assemblies; not a WPF build).'
& (Join-Path $outDir 'ApexSenseBridgeTray.LearningTests.exe')
if ($LASTEXITCODE -ne 0) { throw "Learning tests failed: $LASTEXITCODE" }
