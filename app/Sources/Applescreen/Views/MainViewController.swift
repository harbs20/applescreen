import AppKit

final class MainViewController: NSViewController {
    private let viewModel = WindowControlViewModel()

    private let statusLabel = NSTextField(labelWithString: "Not connected")
    private let javaPathField = NSTextField(string: "")
    private let javaArgsField = NSTextField(string: "")
    private let corePathField = NSTextField(string: defaultCorePath())
    private let xField = NSTextField(string: "0")
    private let yField = NSTextField(string: "0")
    private let wField = NSTextField(string: "854")
    private let hField = NSTextField(string: "480")
    private let logView = NSTextView()

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 640, height: 560))
        buildUI()
        wireViewModel()
    }

    private func wireViewModel() {
        viewModel.onLog = { [weak self] message in
            self?.appendLog(message)
        }
        viewModel.onConnectionChange = { [weak self] connected in
            self?.statusLabel.stringValue = connected ? "Connected" : "Not connected"
        }
    }

    private func appendLog(_ message: String) {
        let stamp = ISO8601DateFormatter().string(from: Date())
        logView.textStorage?.append(NSAttributedString(string: "[\(stamp)] \(message)\n"))
        logView.scrollToEndOfDocument(nil)
    }

    private func buildUI() {
        let root = NSStackView()
        root.orientation = .vertical
        root.alignment = .leading
        root.spacing = 10
        root.edgeInsets = NSEdgeInsets(top: 14, left: 14, bottom: 14, right: 14)
        root.translatesAutoresizingMaskIntoConstraints = false

        root.addArrangedSubview(row(label: "Status:", control: statusLabel, button: ClosureButton(title: "Connect") { [weak self] in
            self?.viewModel.connect()
        }))

        root.addArrangedSubview(sectionLabel("Launch"))
        root.addArrangedSubview(labeledField("Java binary path:", javaPathField, width: 380))
        root.addArrangedSubview(labeledField("Java arguments (space-separated):", javaArgsField, width: 380))
        root.addArrangedSubview(labeledField("Core dylib path:", corePathField, width: 380))
        root.addArrangedSubview(buttonRow([
            ClosureButton(title: "Inspect Java Signing") { [weak self] in self?.onInspectSigning() },
            ClosureButton(title: "Launch Minecraft") { [weak self] in self?.onLaunch() },
        ]))

        root.addArrangedSubview(sectionLabel("Window Control"))
        root.addArrangedSubview(buttonRow([ClosureButton(title: "Ping") { [weak self] in self?.viewModel.ping() }]))

        let posRow = NSStackView(views: [
            NSTextField(labelWithString: "X:"), xField,
            NSTextField(labelWithString: "Y:"), yField,
            ClosureButton(title: "Set Position") { [weak self] in self?.onSetPosition() },
            ClosureButton(title: "Get Position") { [weak self] in self?.viewModel.getPosition() },
        ])
        posRow.spacing = 6
        root.addArrangedSubview(posRow)

        let sizeRow = NSStackView(views: [
            NSTextField(labelWithString: "W:"), wField,
            NSTextField(labelWithString: "H:"), hField,
            ClosureButton(title: "Set Size") { [weak self] in self?.onSetSize() },
            ClosureButton(title: "Get Size") { [weak self] in self?.viewModel.getSize() },
        ])
        sizeRow.spacing = 6
        root.addArrangedSubview(sizeRow)

        root.addArrangedSubview(buttonRow([
            ClosureButton(title: "Focus Window") { [weak self] in self?.viewModel.focusWindow() },
        ]))

        root.addArrangedSubview(sectionLabel("Log"))

        let scrollView = NSScrollView()
        scrollView.hasVerticalScroller = true
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        logView.isEditable = false
        logView.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        scrollView.documentView = logView
        logView.autoresizingMask = [.width]
        scrollView.widthAnchor.constraint(equalToConstant: 600).isActive = true
        scrollView.heightAnchor.constraint(equalToConstant: 220).isActive = true
        root.addArrangedSubview(scrollView)

        view.addSubview(root)
        NSLayoutConstraint.activate([
            root.topAnchor.constraint(equalTo: view.topAnchor),
            root.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            root.trailingAnchor.constraint(lessThanOrEqualTo: view.trailingAnchor),
            root.bottomAnchor.constraint(lessThanOrEqualTo: view.bottomAnchor),
        ])
    }

    private func onInspectSigning() {
        viewModel.inspectSigning(javaPath: javaPathField.stringValue)
    }

    private func onLaunch() {
        let args = javaArgsField.stringValue.split(separator: " ").map(String.init)
        viewModel.launchMinecraft(javaPath: javaPathField.stringValue, corePath: corePathField.stringValue, javaArgs: args)
    }

    private func onSetPosition() {
        guard let x = Int32(xField.stringValue), let y = Int32(yField.stringValue) else {
            appendLog("invalid X/Y")
            return
        }
        viewModel.setPosition(x: x, y: y)
    }

    private func onSetSize() {
        guard let w = Int32(wField.stringValue), let h = Int32(hField.stringValue) else {
            appendLog("invalid W/H")
            return
        }
        viewModel.setSize(w: w, h: h)
    }

    // MARK: - layout helpers

    private func sectionLabel(_ text: String) -> NSView {
        let label = NSTextField(labelWithString: text)
        label.font = NSFont.boldSystemFont(ofSize: 12)
        return label
    }

    private func row(label text: String, control: NSView, button: NSView) -> NSView {
        let label = NSTextField(labelWithString: text)
        let stack = NSStackView(views: [label, control, button])
        stack.spacing = 8
        return stack
    }

    private func labeledField(_ label: String, _ field: NSTextField, width: CGFloat) -> NSView {
        field.translatesAutoresizingMaskIntoConstraints = false
        field.widthAnchor.constraint(equalToConstant: width).isActive = true
        let stack = NSStackView(views: [NSTextField(labelWithString: label), field])
        stack.spacing = 8
        return stack
    }

    private func buttonRow(_ buttons: [NSView]) -> NSView {
        let stack = NSStackView(views: buttons)
        stack.spacing = 8
        return stack
    }
}

private func defaultCorePath() -> String {
    // Points at the debug build produced by `cmake --build build` from the
    // repo root - adjust for a release build or a different checkout path.
    let repoRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent() // Views
        .deletingLastPathComponent() // Applescreen
        .deletingLastPathComponent() // Sources
        .deletingLastPathComponent() // app
    return repoRoot.appendingPathComponent("build/core/libapplescreen_core.dylib").path
}
