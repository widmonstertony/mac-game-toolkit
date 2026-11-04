import AppKit
import Foundation

private let runtimeHome = URL(fileURLWithPath: NSHomeDirectory())
    .appendingPathComponent("Library/Application Support/com.tencent.yybmac.wine.engine/FH6Runtime", isDirectory: true)

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var launched = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        DispatchQueue.main.async { [weak self] in self?.launchOnce() }
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        launchOnce()
    }

    private func launchOnce() {
        guard !launched else { return }
        launched = true

        let info = Bundle.main.infoDictionary ?? [:]
        let displayName = info["CFBundleName"] as? String ?? "应用宝 Wine 11 桥接器"
        guard let windowsExecutable = info["YYBWindowsExecutable"] as? String,
              !windowsExecutable.isEmpty else {
            showError("入口配置不完整", "缺少 YYBWindowsExecutable。")
            return
        }

        let executable = runtimeHome.appendingPathComponent("bin/highball")
        guard FileManager.default.isExecutableFile(atPath: executable.path) else {
            showError("Wine 11 运行环境不存在", "应用宝的 FH6Runtime 缺少启动程序。")
            return
        }

        let logName = info["YYBBridgeLog"] as? String ?? "generic-bridge.log"
        let logURL = runtimeHome.appendingPathComponent(logName)
        FileManager.default.createFile(atPath: logURL.path, contents: nil)
        let log = try? FileHandle(forWritingTo: logURL)
        _ = try? log?.seekToEnd()

        let process = Process()
        process.executableURL = executable
        var arguments = ["run", "FH6-Clean", windowsExecutable]
        if let windowsArguments = info["YYBWindowsArguments"] as? [String], !windowsArguments.isEmpty {
            arguments.append("--")
            arguments.append(contentsOf: windowsArguments)
        }
        process.arguments = arguments

        // Application Treasure invokes this bridge through its own Wine.
        // Never leak that Wine's loader/renderer variables into Wine 11.
        let inherited = ProcessInfo.processInfo.environment
        let allowedKeys = [
            "HOME", "USER", "LOGNAME", "TMPDIR", "LANG", "LC_ALL",
            "LC_CTYPE", "SHELL", "SSH_AUTH_SOCK", "XPC_FLAGS",
            "XPC_SERVICE_NAME", "__CFBundleIdentifier"
        ]
        var environment: [String: String] = [:]
        for key in allowedKeys {
            if let value = inherited[key] { environment[key] = value }
        }
        environment["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
        environment["HIGHBALL_HOME"] = runtimeHome.path

        process.environment = environment
        process.currentDirectoryURL = runtimeHome
        process.standardOutput = log
        process.standardError = log

        do {
            try process.run()
        } catch {
            showError("无法启动 \(displayName)", error.localizedDescription)
            return
        }
        NSApp.terminate(nil)
    }

    private func showError(_ message: String, _ detail: String) {
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = detail
        alert.runModal()
        NSApp.terminate(nil)
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
