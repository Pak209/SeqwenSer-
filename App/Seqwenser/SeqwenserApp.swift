import SwiftUI
import SeqwenserKit

@main
struct SeqwenserApp: App {
    @StateObject private var model = AppModel()
    var body: some Scene {
        WindowGroup {
            MainView()
                .environmentObject(model)
                .onAppear { model.applyLaunchArguments(ProcessInfo.processInfo.arguments) }
        }
    }
}
