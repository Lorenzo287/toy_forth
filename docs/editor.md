# Editors and Formatting

The Toy SDK includes `toyfmt`, the `toy-lsp` language server, the `toy-dap`
debug adapter, and generated Tree-sitter sources. The repository also contains
a VS Code extension for syntax highlighting and LSP integration.

## Formatter

`toyfmt` preserves a program's line breaks while normalizing indentation and
horizontal whitespace. It understands Toy's token boundaries, leaves strings
and comments untouched, and rejects malformed input. Configuration lives in
`.toyfmt` files.

```toml
indent_width = 4
indent_style = "spaces"
delimiter_spacing = "spaced"
```

`indent_style` accepts `"spaces"`, `"tab"`, or `"tabs"`.
`delimiter_spacing` accepts `"spaced"` (`[ 1 2 ]`) or `"compact"`
(`[1 2]`). Set `disable = true` for formatting-sensitive source such as a
quine. Command-line flags override project configuration.

Vectors use a small layout hint: a newline immediately after `[` makes the
matching `]` multiline and aligned with the opener. Inline vectors keep their
existing line layout, including comments and nested statements. Other
delimiters do not use this hint.

The language server uses the same formatter for
`textDocument/formatting`. With an LSP client attached, Neovim can format a
buffer with:

```lua
vim.lsp.buf.format({ async = false })
```

## Language Server

`toy-lsp` indexes Toy files with Tree-sitter and provides definitions,
references, rename, document symbols, builtin, source, and official `core:`
package documentation on hover, and formatting. It understands top-level
definitions, captures, privacy, qualified package words, and literal `import`
and `import-as` paths.
Rename and references cover every direct source file in a package directory and
the workspace files that import that package. Open buffers override files on
disk during analysis.

The server is installed with Toy. A minimal Neovim setup is:

```lua
vim.lsp.config("toyls", {
    cmd = { "toy-lsp" },
    filetypes = { "toy" },
    root_markers = { ".git", "README.md" },
})

vim.lsp.enable("toyls")
```

Imports assembled dynamically during evaluation have no static target.
Packages implemented by C extensions likewise have no Toy definition file to
open, and the server does not load arbitrary binaries to discover their
documentation. Generated metadata provides hover documentation for official
`core:` packages such as `core:ffi`. For `core:` source navigation, the server
uses a `core` directory beneath the workspace root when one is present.

## Debugger

`toy-dap` speaks the standard
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/).
It launches Toy programs, sets source breakpoints, steps into, over, and out,
and exposes VM frames, the data stack, and captures to the editor. Output and
diagnostics appear in the debug console.

With `nvim-dap`, register the installed adapter and a launch configuration:

```lua
local dap = require("dap")

dap.adapters.toy = {
    type = "executable",
    command = "toy-dap",
    options = { detached = false },
}

dap.configurations.toy = {
    {
        type = "toy",
        request = "launch",
        name = "Debug current Toy file",
        program = "${file}",
        cwd = "${workspaceFolder}",
        stopOnEntry = true,
        args = {},
    },
}
```

Breakpoints belong to the launched source file. Stack and capture values are
shown in Toy source form. The adapter finds the `toy` executable beside itself
and then on `PATH`; `runtimeExecutable` can select another one.

`toy --debug-protocol` is the adapter's private transport. Editor integrations
should use DAP instead of parsing that stream.

## Tree-sitter and Neovim

The SDK installs the generated parser and queries under
`share/toy/tree-sitter-toy`. Register that directory with
`nvim-treesitter`:

```lua
local toy_path = vim.fn.expand("$LOCALAPPDATA/Toy/share/toy/tree-sitter-toy")
-- Linux/macOS default:
-- local toy_path = vim.fn.expand("~/.local/share/toy/share/toy/tree-sitter-toy")

vim.filetype.add({
    extension = { fth = "toy", tf = "toy", toy = "toy" },
})

local function add_toy_parser()
    require("nvim-treesitter.parsers").toy = {
        install_info = {
            path = toy_path,
            queries = "queries/toy",
        },
    }
end

add_toy_parser()
vim.api.nvim_create_autocmd("User", {
    pattern = "TSUpdate",
    callback = add_toy_parser,
})

vim.treesitter.language.register("toy", "toy")
```

Restart Neovim and run `:TSInstall! toy`. On Windows, close every Neovim
instance before replacing an installed parser: Windows will not overwrite a
loaded `toy.so`. Stale parsers live under
`%LOCALAPPDATA%\nvim-data\site\parser\` and the corresponding
`lazy\nvim-treesitter\parser\` directory.

## VS Code

The extension under `tools/vscode-toy/` supplies syntax highlighting, comment
and bracket configuration, and an LSP client. It prefers its bundled
`toy-lsp`, then searches `PATH`; the `toy.lsp.path` setting can point to another
executable.

To build and install the extension from the repository:

```console
cd tools/vscode-toy
npm install
npx @vscode/vsce package
```

Install the resulting `.vsix` through **Extensions: Install from VSIX...** in
the VS Code command palette.
