# Toy Forth for VS Code

Visual Studio Code extension for Toy Forth, providing syntax highlighting and integration with the Toy Forth Language Server (LSP).

## Features

- **Syntax Highlighting**: Comprehensive TextMate-based highlighting for words, symbols, variables, and control flow.
- **LSP Client**: Seamless integration with `toyforth-lsp` for definitions, hovers, and symbols.
- **Language Configuration**: Support for comments, brackets, and auto-closing pairs.

## Installation

### Quick Install

1. Open a terminal in `tools/vscode-toyforth`.
2. Package the extension (requires `npm install -g @vscode/vsce`):
   ```powershell
   vsce package
   ```
3. Install the generated `.vsix` file:
   - Open VS Code Command Palette (`Ctrl+Shift+P`).
   - Search for **Extensions: Install from VSIX...**.
   - Select the `vscode-toyforth-0.2.0.vsix` file.

## Development

The extension source is in `tools/vscode-toyforth/extension.js`. It expects the LSP executable to be located in a `bin/` subdirectory within the extension folder.

From `tools/vscode-toyforth`:

```powershell
# Install dependencies
npm install
```
