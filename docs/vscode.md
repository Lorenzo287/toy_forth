# Toy Forth for VS Code

Visual Studio Code extension for Toy Forth, providing syntax highlighting and integration with the Toy Forth Language Server (LSP).

## Features

- **Syntax Highlighting**: Comprehensive TextMate-based highlighting for words, symbols, variables, and control flow.
- **LSP Client**: Seamless integration with `toyforth-lsp` for definitions, hovers, and symbols.
- **Language Configuration**: Support for comments, brackets, and auto-closing pairs.

## Setup & Installation

The extension expects the LSP executable to be located in a `bin/` subdirectory within the extension folder (`tools/vscode-toyforth/bin/`).

### 1. Prepare the LSP Binary
From `tools/toyforth-lsp`, build the LSP and copy it to the extension's `bin` folder:
```powershell
go build -o ../vscode-toyforth/bin/toyforth-lsp.exe ./cmd/toyforth-lsp
```

### 2. Package & Install
From `tools/vscode-toyforth`:

1. Package the extension (requires `npm install -g @vscode/vsce`):
   ```powershell
   vsce package
   ```
2. Install the generated `.vsix` file:
   - Open VS Code Command Palette (`Ctrl+Shift+P`).
   - Search for **Extensions: Install from VSIX...**.
   - Select the `vscode-toyforth-0.2.0.vsix` file.

## Development

The extension source is in `tools/vscode-toyforth/extension.js`. 

To set up the development environment, run from `tools/vscode-toyforth`:
```powershell
npm install
```
