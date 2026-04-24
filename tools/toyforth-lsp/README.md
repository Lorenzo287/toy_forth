# Toy Forth LSP

Minimal standalone Language Server Protocol implementation for Toy Forth.

The server is written in Go and uses the existing Tree-sitter parser from
`tools\tree-sitter-toyforth` for indexing.

## Current Scope

Supported LSP methods:

- `initialize`
- `shutdown`
- `exit`
- `textDocument/didOpen`
- `textDocument/didChange`
- `textDocument/didClose`
- `textDocument/documentSymbol`
- `textDocument/definition`
- `textDocument/hover`
- `textDocument/references`
- `textDocument/rename`

## Current Semantics

Indexing is same-file only.

Top-level definitions:

- `: name ... ;`
- `'name [ ... ] def`

Locals:

- bindings from `{ a b }`
- fetches like `$a`
- nested block shadowing inside `[ ... ]`

Current features:

- `documentSymbol` shows top-level definitions.
- `definition` jumps to the start of the defined word, not to `:` or `'`.
- `hover` shows builtin docs and user-defined docs.
- leading `\ comment` lines above a definition are shown in hover.
- stack-effect comments like `( in -- out )` inside colon definitions are shown in hover.
- `rename` works within the current file for both top-level words and locals.

Current limitations:

- no workspace-wide indexing yet
- no cross-file definition, hover, references, or rename
- no diagnostics or completion yet

## Run

From `tools\toyforth-lsp`:

```powershell
go run .\cmd\toyforth-lsp
```

Or build once and run the executable:

```powershell
go build -o toyforth-lsp.exe .\cmd\toyforth-lsp
.\toyforth-lsp.exe
```

## Neovim

```lua
vim.filetype.add({
  extension = {
    fth = 'toyforth',
    tf = 'toyforth',
  },
})

vim.lsp.config('toyforth_lsp', {
  cmd = {
    'go',
    '-C',
    'C:\\Users\\ltumi\\OneDrive\\CLOUD\\CODE\\C\\antirez\\toy_forth\\tools\\toyforth-lsp',
    'run',
    '.\\cmd\\toyforth-lsp',
  },
  filetypes = { 'toyforth' },
  root_markers = { '.git', 'README.md' },
})

vim.lsp.enable('toyforth_lsp')
```

If you prefer a built executable:

```lua
vim.lsp.config('toyforth_lsp', {
  cmd = {
    'C:\\Users\\ltumi\\OneDrive\\CLOUD\\CODE\\C\\antirez\\toy_forth\\tools\\toyforth-lsp\\toyforth-lsp.exe',
  },
  filetypes = { 'toyforth' },
  root_markers = { '.git', 'README.md' },
})

vim.lsp.enable('toyforth_lsp')
```

Useful Neovim commands:

```lua
vim.lsp.buf.document_symbol()
vim.lsp.buf.definition()
vim.lsp.buf.hover()
vim.lsp.buf.references()
vim.lsp.buf.rename()
```

## Example Fixture

The sample file in `testdata\symbols.fth` exercises the current server behavior:

```forth
\ top-level definitions
: square ( n -- n*n ) { n } $n $n * ;

\ definition comment
'cube [ { n } $n $n $n * * ] def

5 square .
3 cube .

: shadow-demo { outer } $outer [ { outer } $outer ] exec $outer ;
```

## Verification

From `tools\toyforth-lsp`:

```powershell
gofmt -w .
go test .\...
```
