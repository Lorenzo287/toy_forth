# install-nvim.ps1
# This script builds Toy Forth tools and installs them to a local directory for Neovim.

$ErrorActionPreference = "Stop"

# Change this path if you prefer a different location
$InstallDir = "C:\toy_forth"

$RepoRoot = (Get-Item $PSScriptRoot).Parent.FullName
$LspSrcDir = Join-Path $RepoRoot "tools\toyforth-lsp"
$TsSrcDir = Join-Path $RepoRoot "tools\tree-sitter-toyforth"

Write-Host "--- Toy Forth Local Installation ---" -ForegroundColor Cyan
Write-Host "Target Installation Directory: $InstallDir" -ForegroundColor Gray

# 1. Create Target Directory
if (-not (Test-Path $InstallDir)) {
    Write-Host "Creating installation directory..."
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# 2. Build the LSP
Write-Host "`n[1/3] Building Toy Forth LSP..." -ForegroundColor Yellow
Push-Location $LspSrcDir
try {
    if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
        Write-Error "Go is not installed. Please install Go to build the LSP."
    }
    go build -o toyforth-lsp.exe ./cmd/toyforth-lsp
    Copy-Item "toyforth-lsp.exe" -Destination $InstallDir -Force
    Write-Host "LSP installed to: $(Join-Path $InstallDir 'toyforth-lsp.exe')" -ForegroundColor Green
} finally {
    Pop-Location
}

# 3. Generate and Copy Tree-sitter files
Write-Host "`n[2/3] Generating Tree-sitter Parser..." -ForegroundColor Yellow
Push-Location $TsSrcDir
try {
    if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
        Write-Error "npm is not installed. Please install Node.js/npm."
    }
    
    Write-Host "Running npm install..."
    npm install --silent
    
    Write-Host "Generating parser.c..."
    if (Get-Command tree-sitter -ErrorAction SilentlyContinue) {
        tree-sitter generate
    } else {
        npx tree-sitter generate
    }

    # Copy the TS folder to the install directory
    # We copy the whole folder because Neovim needs 'src/parser.c' and the 'queries/' directory.
    $TsDest = Join-Path $InstallDir "tree-sitter-toyforth"
    if (Test-Path $TsDest) { Remove-Item $TsDest -Recurse -Force }
    
    # We only need specific subfolders for Neovim to function
    New-Item -ItemType Directory -Path $TsDest -Force | Out-Null
    Copy-Item "src" -Destination $TsDest -Recurse
    Copy-Item "queries" -Destination $TsDest -Recurse
    Copy-Item "grammar.js" -Destination $TsDest
    Copy-Item "tree-sitter.json" -Destination $TsDest
    
    Write-Host "Tree-sitter files installed to: $TsDest" -ForegroundColor Green
} finally {
    Pop-Location
}

# 4. Output Neovim Configuration
$LspPathEscaped = (Join-Path $InstallDir "toyforth-lsp.exe").Replace('\', '\\')
$TsPathEscaped = (Join-Path $InstallDir "tree-sitter-toyforth").Replace('\', '/')

Write-Host "`n[3/3] --- Neovim Configuration Snippet ---" -ForegroundColor Cyan
Write-Host "Add the following to your init.lua:" -ForegroundColor Gray

$ConfigSnippet = @"
-- Toy Forth LSP Configuration
vim.lsp.config("toyforth_lsp", {
    cmd = { "$LspPathEscaped" },
    filetypes = { "toyforth" },
    root_markers = { ".git", "README.md" },
})
vim.lsp.enable("toyforth_lsp")

-- Toy Forth Tree-sitter Configuration
require("nvim-treesitter.parsers").get_parser_configs().toyforth = {
    install_info = {
        url = "$TsPathEscaped",
        files = { "src/parser.c" },
        branch = "main",
    },
    filetype = "toyforth",
}

-- Add queries and parser info to runtime path
vim.opt.rtp:append("$TsPathEscaped")

-- Register filetypes
vim.filetype.add({
    extension = {
        fth = "toyforth",
        tf = "toyforth",
    },
})
"@

Write-Host "`n$ConfigSnippet" -ForegroundColor White
Write-Host "`nAfter updating your config, restart Neovim and run :TSInstall toyforth" -ForegroundColor Yellow
