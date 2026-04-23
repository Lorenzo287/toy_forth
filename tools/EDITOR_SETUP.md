# Toy Forth Editor Setup

Instructions for enabling syntax highlighting and Tree-sitter support.

## Neovim

### 1. Register the Parser
Add this to your Neovim configuration (adjust the `path` to where you cloned the repo):

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

### 2. Install
Run `:TSInstall toyforth` inside Neovim.

---

## VS Code

The extension in `tools/vscode-toyforth` provides standard TextMate highlighting.

### Quick Install
1. Open a terminal in `tools/vscode-toyforth`.
2. Package the extension: `vsce package` (requires `npm install -g @vscode/vsce`).
3. Install the generated `.vsix` file:
   - Command Palette (`Ctrl+Shift+P`) -> "Extensions: Install from VSIX...".
   - Select `vscode-toyforth-0.1.0.vsix`.
