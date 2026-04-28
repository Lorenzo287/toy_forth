# Toy Forth Tree-sitter

Tree-sitter grammar for Toy Forth. It provides a robust, incremental parser that powers syntax highlighting, indentation, and code folding for modern editors.

## Features

- **Syntax Highlighting**: Precise, scope-based highlighting via Tree-sitter queries.
- **Indentation**: Automatic indentation rules for blocks `[ ]`, definitions `: ;`, and local variable lists `{ }`.
- **Folding**: Logical folding ranges for definitions and blocks.

## Editor Setup

### Neovim

The easiest way to set up Tree-sitter and the LSP in Neovim is to use the automated script:

- **Windows**: `.\tools\install-nvim.ps1`
- **Linux/macOS**: `bash tools/install-nvim.sh`

Follow the printed instructions to update your `init.lua`.

Alternatively, you can register the parser manually:

```lua
local toyforth_path = "~/path/to/toy_forth/tools/tree-sitter-toyforth"

-- 1. Register the parser
require("nvim-treesitter.parsers").get_parser_configs().toyforth = {
    install_info = {
        url = toyforth_path,
        files = { "src/parser.c" },
        branch = "main",
    },
    filetype = "toyforth",
}

-- 2. Add queries to runtimepath (so highlighting works)
vim.opt.rtp:append(toyforth_path)

-- 3. Register filetypes
vim.filetype.add({
    extension = {
        fth = "toyforth",
        tf = "toyforth",
    },
})
```

After adding the configuration, run `:TSInstall toyforth` inside Neovim.

> [!NOTE]
> The configuration above uses a local path and requires that you have already generated the parser code. Run `tree-sitter generate` inside the `tools/tree-sitter-toyforth` directory before installing.
>
> Alternatively, you can configure `nvim-treesitter` to fetch and build the parser automatically from a remote GitHub repository. For details on advanced setup without local cloning, refer to the [nvim-treesitter documentation](https://github.com/nvim-treesitter/nvim-treesitter).

## Development

Requires the [tree-sitter CLI](https://github.com/tree-sitter/tree-sitter/blob/master/crates/cli/README.md).

### Build & Test

From `tools/tree-sitter-toyforth`:

```powershell
# Generate the parser from grammar.js
tree-sitter generate

# Run the test suite
tree-sitter test

# Open the interactive playground
npm run start
```
