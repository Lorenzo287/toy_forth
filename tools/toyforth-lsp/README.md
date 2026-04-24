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

### Run from Source

From `tools/toyforth-lsp`:

```powershell
go run ./cmd/toyforth-lsp
```

### Build Executable

```powershell
go build -o toyforth-lsp.exe ./cmd/toyforth-lsp
./toyforth-lsp.exe
```

## Editor Setup

### Neovim

Register the LSP in your configuration:

```lua
vim.lsp.config('toyforth_lsp', {
  cmd = {
    'go',
    '-C',
    'C:\\path\\to\\toy_forth\\tools\\toyforth-lsp',
    'run',
    '.\\cmd\\toyforth-lsp',
  },
  filetypes = { 'toyforth' },
  root_markers = { '.git', 'README.md' },
})

vim.lsp.enable('toyforth_lsp')
```

## Development

### Verification

```powershell
gofmt -w .
go test ./...
```

### Example Fixture

The sample file in `testdata/symbols.fth` exercises the current server behavior.
