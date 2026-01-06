
param([string]$path)

if (-not (Test-Path $path)) {
    Write-Error "File not found: $path"
    exit 1
}

# Read file bytes
$bytes = [System.IO.File]::ReadAllBytes($path)

# Function to extract ASCII strings
function Get-AsciiStrings($bytes) {
    $minLen = 4
    $currentStr = ""
    foreach ($byte in $bytes) {
        $char = [char]$byte
        if ($char -match "[a-zA-Z0-9_\-\. ]") {
            $currentStr += $char
        } else {
            if ($currentStr.Length -ge $minLen) {
                if ($currentStr -match "Init|Reset|Gaming|Zone|Profile|Color|Set|Get|Method|WMI|Acer|Predator|6AF4F258|95764E09|Health|Battery|Fan") {
                    Write-Output "ASCII: $currentStr"
                }
            }
            $currentStr = ""
        }
    }
}

# Function to extract UTF-16 strings
function Get-UnicodeStrings($bytes) {
    try {
        $encoding = [System.Text.Encoding]::Unicode
        $string = $encoding.GetString($bytes)
        # Regex for unicode sequences (naive approach might be slow on huge files, but safe here)
        # Using [char] loops for better control similar to 'strings' util
        
        $chars = $string.ToCharArray()
        $minLen = 4
        $currentStr = ""
        
        foreach ($char in $chars) {
            if ($char -match "[a-zA-Z0-9_\-\. ]") {
                $currentStr += $char
            } else {
                if ($currentStr.Length -ge $minLen) {
                     if ($currentStr -match "Init|Reset|Gaming|Zone|Profile|Color|Set|Get|Method|WMI|Acer|Predator|6AF4F258|95764E09|Health|Battery|Fan") {
                        Write-Output "UTF16: $currentStr"
                    }
                }
                $currentStr = ""
            }
        }
    } catch {
        Write-Error "Error parsing Unicode: $_"
    }
}

Get-AsciiStrings $bytes
Get-UnicodeStrings $bytes
