import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        ZStack {
            HomeView()
            if model.screen == .player {
                PlayerView()
                    .transition(.circleReveal)
                    .zIndex(1)
            }
            if model.screen == .radio {
                RadioView()
                    .transition(.move(edge: .top).combined(with: .opacity))
                    .zIndex(3)
            }
            if model.screen == .perform {
                PerformanceView(deck: model.performance)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
                    .zIndex(2)
            }
            if model.screen == .chop, let chop = model.chop {
                ChopView(session: chop)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
                    .zIndex(2)
            }
        }
        .ignoresSafeArea()
        .preferredColorScheme(.light)
        .onDrop(of: [.fileURL], isTargeted: $model.isDropTargeted) { providers in
            for provider in providers {
                _ = provider.loadObject(ofClass: URL.self) { url, _ in
                    guard let url else { return }
                    Task { @MainActor in AppModel.shared.importItems([url]) }
                }
            }
            return true
        }
    }
}

/// The player opens out of the mark like an iris.
private struct CircleReveal: ViewModifier {
    var progress: CGFloat

    func body(content: Content) -> some View {
        content.mask {
            GeometryReader { geo in
                let d = hypot(geo.size.width, geo.size.height) * 1.1 * progress
                Circle()
                    .frame(width: d, height: d)
                    .position(x: geo.size.width / 2, y: geo.size.height * 0.42)
            }
        }
    }
}

extension AnyTransition {
    static var circleReveal: AnyTransition {
        .modifier(active: CircleReveal(progress: 0.001), identity: CircleReveal(progress: 1))
    }
}
