import * as vscode from 'vscode';
import * as path from 'path';
import * as net from 'net';
import * as cp from 'child_process';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    StreamInfo,
} from 'vscode-languageclient/node';

// ── Global state ───────────────────────────────────────────────
let client: LanguageClient | undefined;
let aiSocket: net.Socket | undefined;
let aiEngineProcess: cp.ChildProcess | undefined;
let diagnosticCollection: vscode.DiagnosticCollection;
let outputChannel: vscode.OutputChannel;
let statusBar: vscode.StatusBarItem;

// ── Activate ───────────────────────────────────────────────────
export async function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Q+ Language');
    diagnosticCollection = vscode.languages.createDiagnosticCollection('qplus');
    context.subscriptions.push(diagnosticCollection);

    // Status bar
    statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    statusBar.text = '$(loading~spin) Q+ Initializing...';
    statusBar.show();
    context.subscriptions.push(statusBar);

    const cfg = vscode.workspace.getConfiguration('qplus');

    // ── Start local AI engine server ──────────────────────────
    const port: number = cfg.get<number>('aiEngine.serverPort', 7777);
    if (cfg.get<boolean>('aiEngine.enabled', true)) {
        await startAIEngine(context, port);
    }

    // ── Register commands ─────────────────────────────────────
    context.subscriptions.push(
        vscode.commands.registerCommand('qplus.compile', () => compileCurrentFile()),
        vscode.commands.registerCommand('qplus.analyze', () => analyzeCurrentFile()),
        vscode.commands.registerCommand('qplus.buildKernel', () => buildKernel()),
        vscode.commands.registerCommand('qplus.runQemu', () => runQemu()),
        vscode.commands.registerCommand('qplus.aiSuggest', () => triggerAISuggest()),
    );

    // ── Register completion provider (AI-backed) ──────────────
    const completionProvider = vscode.languages.registerCompletionItemProvider(
        { scheme: 'file', language: 'qplus' },
        new QPlusCompletionProvider(),
        '.', '::', '(',
    );
    context.subscriptions.push(completionProvider);

    // ── Register hover provider ────────────────────────────────
    const hoverProvider = vscode.languages.registerHoverProvider(
        { scheme: 'file', language: 'qplus' },
        new QPlusHoverProvider(),
    );
    context.subscriptions.push(hoverProvider);

    // ── On save: run security scan ────────────────────────────
    if (cfg.get<boolean>('security.runOnSave', true)) {
        context.subscriptions.push(
            vscode.workspace.onDidSaveTextDocument(doc => {
                if (doc.languageId === 'qplus') {
                    runSecurityScan(doc);
                }
            })
        );
    }

    // ── On open: run security scan ────────────────────────────
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (doc.languageId === 'qplus') {
                runSecurityScan(doc);
            }
        })
    );

    statusBar.text = '$(check) Q+ Ready';
    outputChannel.appendLine('[Q+] Extension activated.');
}

// ── AI Engine management ───────────────────────────────────────

async function startAIEngine(context: vscode.ExtensionContext, port: number) {
    const enginePath = path.join(context.extensionPath, '..', 'ai_engine', 'server.py');
    const stdlibPath = vscode.workspace.getConfiguration('qplus').get<string>('stdlibPath', '');

    try {
        const args = [`--port=${port}`];
        if (stdlibPath) args.push(`--stdlib=${stdlibPath}`);

        aiEngineProcess = cp.spawn('python3', [enginePath, ...args], {
            stdio: ['pipe', 'pipe', 'pipe'],
        });

        aiEngineProcess.stderr?.on('data', (d: Buffer) => {
            outputChannel.appendLine('[AI Engine] ' + d.toString().trim());
        });

        aiEngineProcess.on('exit', (code) => {
            outputChannel.appendLine(`[AI Engine] Exited with code ${code}`);
        });

        // Wait up to 3s for the engine to start
        await new Promise<void>((resolve) => setTimeout(resolve, 1500));

        // Connect
        await connectAISocket(port);
        outputChannel.appendLine('[Q+] AI Engine connected on port ' + port);
    } catch (e) {
        outputChannel.appendLine('[Q+] Could not start AI engine: ' + e);
    }
}

function connectAISocket(port: number): Promise<void> {
    return new Promise((resolve, reject) => {
        aiSocket = net.createConnection({ port, host: '127.0.0.1' }, () => resolve());
        aiSocket.on('error', reject);
    });
}

let _reqId = 1;
function aiRequest(method: string, params: object): Promise<any> {
    return new Promise((resolve, reject) => {
        if (!aiSocket || aiSocket.destroyed) {
            reject(new Error('AI engine not connected'));
            return;
        }
        const id = _reqId++;
        const payload = JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n';

        const onData = (data: Buffer) => {
            try {
                const resp = JSON.parse(data.toString().trim());
                if (resp.id === id) {
                    aiSocket?.off('data', onData);
                    if (resp.error) reject(new Error(resp.error.message));
                    else resolve(resp.result);
                }
            } catch { /* accumulate */ }
        };
        aiSocket.on('data', onData);
        aiSocket.write(payload);
    });
}

// ── Completion Provider ────────────────────────────────────────

class QPlusCompletionProvider implements vscode.CompletionItemProvider {
    async provideCompletionItems(
        document: vscode.TextDocument,
        position: vscode.Position,
    ): Promise<vscode.CompletionItem[]> {
        const lineText = document.lineAt(position).text.substring(0, position.character);
        const wordMatch = lineText.match(/[\w:]+$/);
        const prefix = wordMatch ? wordMatch[0].replace(/^::/, '') : '';

        // Detect if we're inside an unsafe block
        const fullText = document.getText();
        const offset = document.offsetAt(position);
        const scope  = isInsideUnsafe(fullText, offset) ? 'unsafe' : 'safe';

        // Extract imports from current file
        const imports = extractImports(fullText);

        try {
            const result = await aiRequest('qplus/complete', {
                prefix,
                context: { imports, scope, cursor_after: lineText.slice(-40) },
            });
            return (result.items || []).map((item: any) => {
                const ci = new vscode.CompletionItem(item.label,
                    kindToVSCode(item.kind));
                ci.detail = item.detail;
                ci.documentation = new vscode.MarkdownString(item.documentation || '');
                ci.insertText = new vscode.SnippetString(item.insertText || item.label);
                ci.sortText = item.sortText;
                return ci;
            });
        } catch {
            return [];
        }
    }
}

function kindToVSCode(kind: string): vscode.CompletionItemKind {
    switch (kind) {
        case 'function': return vscode.CompletionItemKind.Function;
        case 'struct':   return vscode.CompletionItemKind.Struct;
        case 'enum':     return vscode.CompletionItemKind.Enum;
        case 'const':    return vscode.CompletionItemKind.Constant;
        case 'module':   return vscode.CompletionItemKind.Module;
        case 'driver':   return vscode.CompletionItemKind.Class;
        case 'syscall':  return vscode.CompletionItemKind.Event;
        case 'keyword':  return vscode.CompletionItemKind.Keyword;
        default:         return vscode.CompletionItemKind.Text;
    }
}

// ── Hover Provider ─────────────────────────────────────────────

class QPlusHoverProvider implements vscode.HoverProvider {
    async provideHover(
        document: vscode.TextDocument,
        position: vscode.Position,
    ): Promise<vscode.Hover | null> {
        const range = document.getWordRangeAtPosition(position);
        if (!range) return null;
        const word  = document.getText(range);
        const fullText = document.getText();
        const imports  = extractImports(fullText);

        try {
            const result = await aiRequest('qplus/complete', {
                prefix: word,
                context: { imports, scope: 'safe' },
            });
            const items = result.items || [];
            const exact = items.find((i: any) => i.label === word);
            if (!exact) return null;

            const md = new vscode.MarkdownString();
            md.appendCodeblock(exact.detail, 'qplus');
            if (exact.documentation) md.appendMarkdown('\n\n' + exact.documentation);
            if (exact.is_unsafe) md.appendMarkdown('\n\n⚠️ **Unsafe** — must be called inside `unsafe { ... }`');
            return new vscode.Hover(md);
        } catch {
            return null;
        }
    }
}

// ── Security scan ──────────────────────────────────────────────

async function runSecurityScan(document: vscode.TextDocument) {
    const source = document.getText();
    try {
        const result = await aiRequest('qplus/security', { source });
        const diags: vscode.Diagnostic[] = (result.findings || []).map((f: any) => {
            const line = Math.max(0, (f.line || 1) - 1);
            const range = new vscode.Range(line, 0, line, 200);
            const sev = f.severity === 'error'   ? vscode.DiagnosticSeverity.Error
                       : f.severity === 'warning' ? vscode.DiagnosticSeverity.Warning
                       :                            vscode.DiagnosticSeverity.Information;
            const diag = new vscode.Diagnostic(range, `[${f.id}] ${f.title}: ${f.description}`, sev);
            diag.source  = 'Q+ Security';
            diag.code    = f.id;
            return diag;
        });
        diagnosticCollection.set(document.uri, diags);
        if (diags.length > 0) {
            statusBar.text = `$(shield) Q+ ${diags.filter(d=>d.severity===0).length} errors, ${diags.filter(d=>d.severity===1).length} warnings`;
        } else {
            statusBar.text = '$(check) Q+ OK';
        }
    } catch {
        // Engine not running
    }
}

// ── Commands ───────────────────────────────────────────────────

async function compileCurrentFile() {
    const doc = vscode.window.activeTextEditor?.document;
    if (!doc || doc.languageId !== 'qplus') {
        vscode.window.showWarningMessage('Open a .qp file first.');
        return;
    }
    const cfg        = vscode.workspace.getConfiguration('qplus');
    const compiler   = cfg.get<string>('compilerPath', 'qpc');
    const terminal   = vscode.window.createTerminal('Q+ Compiler');
    terminal.show();
    terminal.sendText(`${compiler} build "${doc.fileName}"`);
}

async function analyzeCurrentFile() {
    const doc = vscode.window.activeTextEditor?.document;
    if (!doc) return;
    await runSecurityScan(doc);
    vscode.window.showInformationMessage('Q+ Security analysis complete. Check Problems panel.');
}

async function buildKernel() {
    const wf = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!wf) { vscode.window.showWarningMessage('Open the Q+ project folder first.'); return; }
    const terminal = vscode.window.createTerminal('Q+ Kernel Build');
    terminal.show();
    terminal.sendText(`cd "${wf}/os_demo" && make`);
}

async function runQemu() {
    const wf = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!wf) return;
    const cfg    = vscode.workspace.getConfiguration('qplus');
    const mem    = cfg.get<string>('qemu.memory', '128M');
    const terminal = vscode.window.createTerminal('Q+ QEMU');
    terminal.show();
    terminal.sendText(`cd "${wf}/os_demo" && make iso && qemu-system-x86_64 -cdrom build/qplus_os.iso -m ${mem} -serial stdio -no-reboot`);
}

async function triggerAISuggest() {
    vscode.commands.executeCommand('editor.action.triggerSuggest');
}

// ── Helpers ────────────────────────────────────────────────────

function isInsideUnsafe(source: string, offset: number): boolean {
    // Very rough heuristic: search backwards for 'unsafe {' without matching '}'
    const before = source.slice(0, offset);
    const lastUnsafe = before.lastIndexOf('unsafe');
    if (lastUnsafe < 0) return false;
    const after = before.slice(lastUnsafe);
    const opens  = (after.match(/\{/g) || []).length;
    const closes = (after.match(/\}/g) || []).length;
    return opens > closes;
}

function extractImports(source: string): string[] {
    const imports: string[] = [];
    for (const m of source.matchAll(/^import\s+([\w:]+)\s*;/gm)) {
        imports.push(m[1]);
    }
    return imports;
}

// ── Deactivate ─────────────────────────────────────────────────
export function deactivate() {
    aiSocket?.destroy();
    aiEngineProcess?.kill();
    client?.stop();
}
