const vscode = require("vscode");
const path = require("path");
const fs = require("fs");

function firstExisting(candidates) {
  for (const p of candidates) {
    if (p && fs.existsSync(p)) return p;
  }
  return "";
}

function resolveCompilerPath() {
  const config = vscode.workspace.getConfiguration("isli");
  let compilerPath = config.get("compilerPath");
  if (compilerPath && String(compilerPath).trim() !== "") {
    return String(compilerPath).trim();
  }

  const candidates = [];
  if (vscode.workspace.workspaceFolders) {
    for (const folder of vscode.workspace.workspaceFolders) {
      const root = folder.uri.fsPath;
      candidates.push(path.join(root, "isli_compiler", "isli.exe"));
      candidates.push(path.join(root, "isli.exe"));
    }
  }
  candidates.push(path.join(__dirname, "..", "..", "isli_compiler", "isli.exe"));
  const localApp = process.env.LOCALAPPDATA;
  if (localApp) {
    candidates.push(path.join(localApp, "Isli", "bin", "isli.exe"));
  }

  const found = firstExisting(candidates);
  return found || "isli.exe";
}

function activate(context) {
  const runCommand = vscode.commands.registerCommand("isli.run", function () {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showErrorMessage("Calistirilacak aktif bir dosya bulunamadi.");
      return;
    }

    const document = editor.document;
    if (document.languageId !== "isli" && !document.fileName.endsWith(".isli")) {
      vscode.window.showErrorMessage("Bu dosya bir .isli dosyasi degil.");
      return;
    }

    if (document.isDirty) {
      document.save();
    }

    const filePath = document.uri.fsPath;

    let terminal = vscode.window.terminals.find((t) => t.name === "Isli Runner");
    if (!terminal) {
      terminal = vscode.window.createTerminal("Isli Runner");
    }
    terminal.show();
    vscode.commands.executeCommand("workbench.action.terminal.clear");

    const compilerPath = resolveCompilerPath();
    const cmdExe = compilerPath === "isli.exe" ? "isli.exe" : `"${compilerPath}"`;
    const cmd = `& ${cmdExe} "${filePath}"`;
    terminal.sendText(cmd);
  });

  context.subscriptions.push(runCommand);

  const statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  statusBar.text = "$(play) Run Isli";
  statusBar.command = "isli.run";
  statusBar.tooltip = "Isli Kodunu Calistir (F5)";
  context.subscriptions.push(statusBar);

  function updateStatusBar() {
    const editor = vscode.window.activeTextEditor;
    if (editor && (editor.document.languageId === "isli" || editor.document.fileName.endsWith(".isli"))) {
      statusBar.show();
    } else {
      statusBar.hide();
    }
  }

  vscode.window.onDidChangeActiveTextEditor(updateStatusBar, null, context.subscriptions);
  updateStatusBar();
}

function deactivate() {}

module.exports = { activate, deactivate };
