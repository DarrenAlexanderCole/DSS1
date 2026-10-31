import AppKit
import SwiftUI

/// Every MIDI message DSS1 hears, newest at the bottom: which port, the raw bytes, what they mean and what DSS1
/// did with them. Twist, press and slide the controller's controls, then Copy Log and paste it in a message.
struct MIDIMonitorView: View {
    @ObservedObject private var hub = MIDIHub.shared
    @State private var copied = false

    private static let time: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(hub.connected.map { "\($0.name) · \($0.endpoints.count) port\($0.endpoints.count == 1 ? "" : "s")" } ?? "No MIDI device")
                        .font(.system(size: 12, weight: .bold))
                    Text("Move each control on your controller, then Copy Log and paste it in chat.")
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button("Clear") { hub.clearLog() }
                Button(copied ? "Copied" : "Copy Log") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(hub.logText, forType: .string)
                    copied = true
                    Task {
                        try? await Task.sleep(for: .seconds(1.5))
                        copied = false
                    }
                }
                .keyboardShortcut("c", modifiers: [.command, .shift])
            }
            .padding(12)
            Divider()
            ScrollViewReader { proxy in
                List(hub.log) { entry in
                    HStack(spacing: 12) {
                        Text(Self.time.string(from: entry.time)).foregroundStyle(.secondary).frame(width: 90, alignment: .leading)
                        Text(entry.port).frame(width: 130, alignment: .leading).lineLimit(1)
                        Text(entry.hex).frame(width: 150, alignment: .leading).lineLimit(1)
                        Text(entry.meaning).frame(width: 170, alignment: .leading).lineLimit(1)
                        Text(entry.action).foregroundStyle(entry.action == "unmapped" ? .secondary : .primary).lineLimit(1)
                    }
                    .font(.system(size: 11).monospaced())
                    .id(entry.id)
                }
                .onChange(of: hub.log.last?.id) { _, id in
                    if let id { proxy.scrollTo(id, anchor: .bottom) }
                }
            }
            if hub.log.isEmpty {
                Text(hub.connected == nil ? "Plug in a controller, or pick it in the MIDI menu." : "Waiting for MIDI…")
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .padding(20)
            }
        }
        .frame(minWidth: 640, minHeight: 320)
    }
}
