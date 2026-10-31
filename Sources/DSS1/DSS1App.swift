import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    /// Zips or folders dropped onto the Dock icon (or opened with "Open With → DSS1").
    func application(_ application: NSApplication, open urls: [URL]) {
        Task { @MainActor in AppModel.shared.importItems(urls) }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Paths passed on the command line are imported straight away.
        let paths = CommandLine.arguments.dropFirst().filter { FileManager.default.fileExists(atPath: $0) }
        if !paths.isEmpty {
            Task { @MainActor in AppModel.shared.importItems(paths.map { URL(fileURLWithPath: $0) }) }
        }

        #if TESTHOOKS
        TestHooks.install()
        #endif
    }
}

@main
struct DSS1App: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = AppModel.shared

    var body: some Scene {
        Window("DSS1", id: "main") {
            ContentView()
                .environmentObject(model)
                .environmentObject(model.player)
                .environmentObject(model.library)
                .frame(minWidth: 1180, minHeight: 720)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1200, height: 780)
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("About DSS1") { AboutPanel.show() }
            }
            CommandGroup(replacing: .newItem) {
                Button("Add Audio…") { model.pickSources() }
                    .keyboardShortcut("o")
                Button("Choose Destination…") { model.pickDestination() }
                    .keyboardShortcut("d", modifiers: [.command, .shift])
                Button("Open Destination in Finder") { model.openDestination() }
                    .keyboardShortcut("f", modifiers: [.command, .shift])
            }
            CommandMenu("Playback") {
                Button("Play / Pause") { model.togglePlayback() }
                    .keyboardShortcut(.space, modifiers: [])
                Button("Next Track") { model.nextTrack() }
                    .keyboardShortcut(.rightArrow, modifiers: .command)
                Button("Previous Track") { model.previousTrack() }
                    .keyboardShortcut(.leftArrow, modifiers: .command)
                Button("Volume Up") { model.player.volume = min(1, model.player.volume + 0.1) }
                    .keyboardShortcut(.upArrow, modifiers: .command)
                Button("Volume Down") { model.player.volume = max(0, model.player.volume - 0.1) }
                    .keyboardShortcut(.downArrow, modifiers: .command)
            }
            CommandMenu("MIDI") {
                MIDIMenu()
            }
            // The pages, in the order you meet them.
            CommandMenu("Go") {
                Button("Drop Audio") { model.showHome() }
                    .keyboardShortcut("1")
                Button("The Crate") { model.showPlayer() }
                    .keyboardShortcut("2")
                Button("Radio") { model.showRadio() }
                    .keyboardShortcut("3")
                Button("Performance Pad") { model.showPerformance() }
                    .keyboardShortcut("4")
                Button("Chop Current Track") { model.openChopForCurrent() }
                    .keyboardShortcut("k")
            }
        }
        Window("MIDI Monitor", id: "midiMonitor") {
            MIDIMonitorView()
        }
        .defaultSize(width: 760, height: 420)

        Settings {
            SettingsView()
                .environmentObject(model)
                .environmentObject(model.player)
        }
    }
}

/// The About window: version plus the credits the bundled libraries ask for.
enum AboutPanel {
    @MainActor
    static func show() {
        let credits = NSMutableAttributedString()
        let body: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor]
        let bold: [NSAttributedString.Key: Any] = [.font: NSFont.boldSystemFont(ofSize: 11), .foregroundColor: NSColor.labelColor]
        func add(_ text: String, _ attrs: [NSAttributedString.Key: Any]) { credits.append(NSAttributedString(string: text, attributes: attrs)) }
        add("Digital Sample Station 1\n\n", bold)
        add("Rubber Band Library", bold)
        add(" (key lock) © Particular Programs Ltd, GNU GPL v2 or later.\n", body)
        add("AudioKit & SoundpipeAudioKit", bold)
        add(" (effects) © AudioKit contributors, MIT licence.\n\n", body)
        add("This build is GPL software; its source is available on request.", body)
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = .center
        credits.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: credits.length))
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: "DSS1",
            .credits: credits,
        ])
        NSApp.activate(ignoringOtherApps: true)
    }
}

/// DSS1 → Settings…
struct SettingsView: View {
    @EnvironmentObject private var model: AppModel
    @EnvironmentObject private var player: AudioPlayer
    @AppStorage("lowLatency") private var lowLatency = true

    var body: some View {
        Form {
            Section("Adding audio") {
                Toggle("Convert everything to WAV", isOn: $model.convertToWAV)
                Text("MP3, FLAC, AIFF and M4A files land in the crate as 24-bit WAVs at their own sample rate. WAVs are copied as they are.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("Audio engine") {
                Toggle("Low latency (256-sample buffer)", isOn: $lowLatency)
                Text("Pads, MIDI and the XY pad answer in about 5 ms. Turn off if you hear crackles; takes effect next launch.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("Playback") {
                Toggle("Level match tracks", isOn: $player.levelMatch)
                Text("Plays every track at about the same loudness. Your files aren't changed. The chop station always plays the raw file.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .frame(width: 440)
        .fixedSize()
    }
}

/// The MIDI menu in the menu bar: device choice and mappings.
struct MIDIMenu: View {
    @ObservedObject private var hub = MIDIHub.shared
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Button("MIDI Monitor…") { openWindow(id: "midiMonitor") }
            .keyboardShortcut("m", modifiers: [.command, .option])
        Divider()
        Picker("Device", selection: $hub.choice) {
            Text("Automatic").tag(MIDIHub.Choice.auto)
            Text("None").tag(MIDIHub.Choice.none)
            ForEach(hub.sources) { s in Text(s.endpoints.count > 1 ? "\(s.name) (\(s.endpoints.count) ports)" : s.name).tag(MIDIHub.Choice.device(s.id)) }
        }
        Button("Rescan Devices") { hub.refresh() }
        Divider()
        if hub.learning != nil { Button("Cancel MIDI Learn") { hub.learning = nil } }
        Button("Clear MIDI Mappings") { hub.clearLearned() }
            .disabled(hub.learned.isEmpty)
    }
}
