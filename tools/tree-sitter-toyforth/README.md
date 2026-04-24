# Toy Forth Tree-sitter

Tree-sitter grammar for Toy Forth. It provides a robust, incremental parser that powers syntax highlighting, indentation, and code folding for modern editors.

## Features

- **Syntax Highlighting**: Precise, scope-based highlighting via Tree-sitter queries.
- **Indentation**: Automatic indentation rules for blocks `[ ]`, definitions `: ;`, and local variable lists `{ }`.
- **Folding**: Logical folding ranges for definitions and blocks.

## Editor Setup

### Neovim

To use this parser in Neovim, you need to register it manually in your configuration (adjust the `path` to where you cloned the repo):

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

## Development

Requires the [tree-sitter CLI](https://github.com/tree-sitter/tree-sitter/tree/master/cli).

### Build & Test

```powershell
# Generate the parser from grammar.js
tree-sitter generate

# Run the test suite
tree-sitter test

# Open the interactive playground
npm run start
```
