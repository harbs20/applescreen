import AppKit

/// NSButton's target-action needs an @objc selector; this wraps a closure so
/// the view-construction code below can stay declarative instead of routing
/// every button through hand-written @objc methods on the view controller.
final class ClosureButton: NSButton {
    private var action_: () -> Void = {}

    convenience init(title: String, action: @escaping () -> Void) {
        self.init(title: title, target: nil, action: nil)
        self.action_ = action
        self.target = self
        self.action = #selector(ClosureButton.fire)
    }

    @objc private func fire() {
        action_()
    }
}
