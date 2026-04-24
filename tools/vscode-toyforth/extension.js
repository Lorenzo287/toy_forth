const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient');
const vscode_languageclient = require('vscode-languageclient');
const path = require('path');

let client;

function activate(context) {
    const exe = process.platform === 'win32' ? 'toyforth-lsp.exe' : 'toyforth-lsp';
    const serverPath = path.join(context.extensionPath, 'bin', exe);

    const serverOptions = {
        command: serverPath,
        transportKind: TransportKind.stdio
    };

    const outputChannel = vscode.window.createOutputChannel('Toy Forth LSP');

    const clientOptions = {
        documentSelector: [
            { language: 'toyforth', scheme: 'file' },
            { language: 'toyforth', scheme: 'untitled' }
        ],
        revealOutputChannelOn: vscode_languageclient.RevealOutputChannelOn.Never,
        errorHandler: {
            error: (message, count) => {
                outputChannel.appendLine(`Error: ${message}`);
                return { action: vscode_languageclient.ErrorAction.Consume };
            },
            closed: () => {
                return { action: vscode_languageclient.CloseAction.DoNotRestart };
            }
        }
    };

    client = new LanguageClient('toyforth', 'Toy Forth LSP', serverOptions, clientOptions);
    client.start();

    context.subscriptions.push(client, outputChannel);
}

function deactivate() {
    if (client) {
        return client.stop();
    }
    return undefined;
}

exports.activate = activate;
exports.deactivate = deactivate;