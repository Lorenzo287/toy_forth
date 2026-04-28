# Toy Forth LSP

Minimal standalone Language Server Protocol implementation for Toy Forth, written in Go. It uses the Tree-sitter parser for indexing and providing IDE features.

## Features

### Supported LSP Methods

- **Navigation**: `textDocument/definition`, `textDocument/references`.
- **Introspection**: `textDocument/hover` (shows builtin docs and stack effects), `textDocument/documentSymbol`.
- **Refactoring**: `textDocument/rename` (works for both top-level words and locals).
- **Lifecycle**: `initialize`, `shutdown`, `exit`, `didOpen`, `didChange`, `didClose`.

### Analysis Scope

- **Top-level definitions**: `: name ... ;` and `'name [ ... ] def`.
- **Locals**: Bindings from `{ a b }`, fetches like `$a`, and nested block shadowing.
- **Documentation**: Leading `\ comment` lines and `( in -- out )` stack effect comments are extracted for hovers.

## Getting Started

> [!IMPORTANT]
> The LSP relies on the Tree-sitter parser for code analysis. You **must** generate the parser in `tools/tree-sitter-toyforth` by running `tree-sitter generate` before running or building the LSP. Without the generated `parser.c`, the Go Tree-sitter bindings will fail to compile or function.

### Run from Source

From `tools/toyforth-lsp`:

```powershell
go run ./cmd/toyforth-lsp
```

### Build Executable

From `tools/toyforth-lsp`:

```powershell
go build -o toyforth-lsp.exe ./cmd/toyforth-lsp
./toyforth-lsp.exe
```

## Editor Setup

### Neovim

You can use the automated installation script to build the LSP and generate the Tree-sitter parser, installing them to a central location:

- **Windows**: `.\tools\install-nvim.ps1`
- **Linux/macOS**: `bash tools/install-nvim.sh`

Follow the instructions printed by the script to update your `init.lua`.

Alternatively, register the LSP manually:

```lua
vim.lsp.config('toyforth_lsp', {
  cmd = { 'path/to/toyforth-lsp.exe' },
  filetypes = { 'toyforth' },
  root_markers = { '.git', 'README.md' },
})

vim.lsp.enable('toyforth_lsp')
```

## Development

### Verification

From `tools/toyforth-lsp`:

```powershell
gofmt -w .
go test ./...
```

### Example Fixture

The sample file in `tools/toyforth-lsp/testdata/symbols.fth` exercises the current server behavior.
