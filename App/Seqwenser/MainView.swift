import SwiftUI
import UniformTypeIdentifiers
import SeqwenserKit

/// Design canvas is 393 x 780 pt; it is scaled to fit any screen.
struct MainView: View {
    @EnvironmentObject var model: AppModel
    @State private var showImporter = false
    @State private var dropTargeted = false

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / 393, geo.size.height / 780)
            ZStack {
                Neon.bg.ignoresSafeArea()
                Canvas393(showImporter: $showImporter)
                    .frame(width: 393, height: 780)
                    .scaleEffect(scale, anchor: .center)
                    .frame(width: geo.size.width, height: geo.size.height)
            }
        }
        .background(Neon.bg.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .sheet(item: $model.sheet) { sheet in
            SheetHost(sheet: sheet).environmentObject(model)
        }
        .fileImporter(isPresented: $showImporter, allowedContentTypes: [.audio], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls): model.importMany(urls)
            case .failure(let e): model.message = "Could not open the file: \(e.localizedDescription)"
            }
        }
        .onDrop(of: [.fileURL, .audio], isTargeted: $dropTargeted) { providers in
            DropImport.handle(providers, model: model)
        }
        .overlay(alignment: .top) {
            if dropTargeted {
                Text("DROP AUDIO TO LOAD")
                    .font(.chunky(16)).foregroundColor(.black)
                    .padding(.horizontal, 18).padding(.vertical, 10)
                    .background(Capsule().fill(Neon.lime)).neonGlow(Neon.lime).padding(.top, 60)
            }
        }
        .overlay(alignment: .bottom) { MessageBanner() }
        .onOpenURL { url in model.importAudio(url: url) }
    }
}

enum DropImport {
    static func handle(_ providers: [NSItemProvider], model: AppModel) -> Bool {
        guard let p = providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) })
                ?? providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.audio.identifier) }) else { return false }
        if p.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
            _ = p.loadObject(ofClass: URL.self) { url, _ in
                guard let url else { return }
                Task { @MainActor in model.importAudio(url: url) }
            }
        } else {
            _ = p.loadFileRepresentation(forTypeIdentifier: UTType.audio.identifier) { url, _ in
                guard let url else { return }
                let copy = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + "-" + url.lastPathComponent)
                try? FileManager.default.copyItem(at: url, to: copy)
                Task { @MainActor in model.importAudio(url: copy) }
            }
        }
        return true
    }
}

struct MessageBanner: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        if let m = model.message {
            Text(m)
                .font(.label(13)).foregroundColor(Neon.text)
                .multilineTextAlignment(.leading)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(RoundedRectangle(cornerRadius: 12).fill(Neon.panel2))
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(Neon.yellow, lineWidth: 1))
                .padding(.horizontal, 16).padding(.bottom, 24)
                .onTapGesture { model.message = nil }
                .task(id: m) {
                    try? await Task.sleep(nanoseconds: 5_000_000_000)
                    if model.message == m { model.message = nil }
                }
                .transition(.move(edge: .bottom).combined(with: .opacity))
                .accessibilityIdentifier("messageBanner")
        }
    }
}

struct Canvas393: View {
    @EnvironmentObject var model: AppModel
    @Binding var showImporter: Bool

    var body: some View {
        VStack(spacing: 9) {
            HeaderBar(showImporter: $showImporter)
            LCDView()
            WaveformStrip()
            StepPads()
            ModeTabs()
            ControlArea()
            TransportBar()
        }
        .padding(.horizontal, 14)
        .padding(.top, 2)
        .padding(.bottom, 6)
    }
}

// MARK: - Header

struct HeaderBar: View {
    @EnvironmentObject var model: AppModel
    @Binding var showImporter: Bool
    var body: some View {
        HStack(alignment: .center) {
            VStack(alignment: .leading, spacing: 1) {
                Text("SEQWENSER")
                    .font(.chunky(25)).foregroundColor(Neon.lime)
                    .neonGlow(Neon.lime, radius: 6, opacity: 0.45)
                HStack(spacing: 4) {
                    ForEach(Array(["SAMPLE", "PLAY", "TWIST", "CREATE"].enumerated()), id: \.offset) { i, t in
                        Text(t).font(.label(9)).foregroundColor(i == 0 ? Neon.cyan : Neon.textDim)
                        if i < 3 { Text(">").font(.label(9)).foregroundColor(Neon.edge) }
                    }
                }
            }
            Spacer()
            MascotView(color: true)
                .frame(width: 40, height: 40)
                .padding(4)
                .keyCap(color: Neon.panel, corner: 8)
                .accessibilityHidden(true)
            Menu {
                Button { model.saveProject() } label: { Label("Save project", systemImage: "square.and.arrow.down") }
                Button { model.refreshProjects(); model.sheet = .projects } label: { Label("Projects", systemImage: "folder") }
                Button { showImporter = true } label: { Label("Import audio…", systemImage: "square.and.arrow.down.on.square") }
                Button { model.sheet = .export } label: { Label("Export bounce", systemImage: "square.and.arrow.up") }
                Button { model.clearPattern() } label: { Label("Clear pattern", systemImage: "eraser") }
                Button { model.newProject() } label: { Label("New (demo kit)", systemImage: "doc") }
                Button { model.sheet = .settings } label: { Label("About & settings", systemImage: "gearshape") }
            } label: {
                Image(systemName: "line.3.horizontal")
                    .font(.system(size: 18, weight: .bold)).foregroundColor(Neon.text)
                    .frame(width: 44, height: 44).keyCap(corner: 10)
            }
            .accessibilityLabel("Menu")
        }
        .frame(height: 50)
    }
}

// MARK: - LCD

struct LCDView: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        HStack(spacing: 8) {
            VStack(alignment: .leading, spacing: 4) {
                Text(model.project.name.uppercased()).font(.lcd(15)).foregroundColor(Neon.lcdInk).lineLimit(1)
                Text(model.project.kitName).font(.lcd(12)).foregroundColor(Neon.lcdInk.opacity(0.75)).lineLimit(1)
                Spacer(minLength: 0)
                Text("BPM \(Int(model.project.bpm.rounded()))").font(.lcd(15)).foregroundColor(Neon.lcdInk)
                Text(model.isPlaying ? "▶ STEP \(model.status.step + 1)" : "■ STOPPED").font(.lcd(11)).foregroundColor(Neon.lcdInk.opacity(0.75))
            }
            .frame(width: 118, alignment: .leading)
            ZStack {
                LCDSkyline()
                MascotView(color: false).padding(.top, 6).padding(.bottom, 2)
            }
            .frame(maxWidth: .infinity)
            VStack(alignment: .leading, spacing: 5) {
                lcdItem("REC", "record") { model.sheet = .sample }
                lcdItem("SAMPLE", "sample") { model.sheet = .sample }
                lcdItem("PATTERN", "pattern") { model.sheet = .pattern }
                lcdItem("FILES", "files") { model.refreshProjects(); model.sheet = .projects }
            }
            .frame(width: 84, alignment: .leading)
        }
        .padding(10)
        .frame(height: 128)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(LinearGradient(colors: [Color(hex: 0xA6B896), Neon.lcd], startPoint: .top, endPoint: .bottom))
        )
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color(hex: 0x0A0C0E), lineWidth: 3))
        .overlay(RoundedRectangle(cornerRadius: 9).stroke(Neon.lcdMid.opacity(0.5), lineWidth: 1).padding(3))
    }

    private func lcdItem(_ t: String, _ id: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 4) {
                Text("▸").font(.lcd(11))
                Text(t).font(.lcd(12))
                Spacer(minLength: 0)
            }
            .foregroundColor(Neon.lcdInk)
            .padding(.vertical, 3).padding(.horizontal, 4)
            .background(RoundedRectangle(cornerRadius: 3).fill(Neon.lcdInk.opacity(0.10)))
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("lcd-\(id)")
    }
}

/// Original pixel skyline with a moon (drawn from rectangles).
struct LCDSkyline: View {
    var body: some View {
        Canvas { ctx, size in
            let u: CGFloat = 4
            let cols = Int(size.width / u), rows = Int(size.height / u)
            let ink = Neon.lcdMid
            // moon
            let mx = cols * 3 / 4, my = 3
            for y in -3...3 { for x in -3...3 where x * x + y * y <= 9 && !((x + 1) * (x + 1) + (y - 1) * (y - 1) <= 8) {
                ctx.fill(Path(CGRect(x: CGFloat(mx + x) * u, y: CGFloat(my + y) * u, width: u, height: u)), with: .color(ink.opacity(0.55)))
            } }
            let heights = [6, 9, 5, 12, 8, 14, 7, 10, 6, 13, 9, 5, 11, 7, 15, 8, 6, 10, 12, 5, 9, 7, 11, 6]
            for c in 0..<cols {
                let h = heights[c / 3 % heights.count] + (c % 3 == 0 ? 0 : 0)
                for r in 0..<min(h, rows) {
                    ctx.fill(Path(CGRect(x: CGFloat(c) * u, y: size.height - CGFloat(r + 1) * u, width: u, height: u)), with: .color(ink.opacity(0.45)))
                }
                // lit windows
                if c % 3 == 1 {
                    for r in stride(from: 3, to: min(h, rows) - 1, by: 3) {
                        ctx.fill(Path(CGRect(x: CGFloat(c) * u, y: size.height - CGFloat(r + 1) * u, width: u, height: u)), with: .color(Neon.lcd.opacity(0.9)))
                    }
                }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 6))
        .accessibilityHidden(true)
    }
}

// MARK: - Waveform with trim markers

struct WaveformStrip: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 10).fill(Color(hex: 0x0E1114))
                Canvas { ctx, size in
                    let (mins, maxs) = model.waveform
                    let n = mins.count
                    guard n > 0 else { return }
                    let mid = size.height / 2
                    let gradient = Gradient(colors: [Neon.cyan, Neon.yellow, Neon.magenta])
                    var path = Path()
                    for i in 0..<n {
                        let x = CGFloat(i) / CGFloat(n) * size.width
                        let top = mid - CGFloat(min(1, maxs[i] * 1.05)) * (mid - 6)
                        let bottom = mid - CGFloat(max(-1, mins[i] * 1.05)) * (mid - 6)
                        path.addRect(CGRect(x: x, y: top, width: max(1, size.width / CGFloat(n) - 0.6), height: max(1.5, bottom - top)))
                    }
                    ctx.fill(path, with: .linearGradient(gradient, startPoint: .zero, endPoint: CGPoint(x: size.width, y: 0)))
                    // slice lines
                    for e in model.project.sliceEdges.dropFirst().dropLast() {
                        ctx.fill(Path(CGRect(x: CGFloat(e) * size.width, y: 0, width: 1, height: size.height)), with: .color(Color.white.opacity(0.28)))
                    }
                    // outside of the trim
                    let a = CGFloat(model.project.trimStart) * size.width, b = CGFloat(model.project.trimEnd) * size.width
                    ctx.fill(Path(CGRect(x: 0, y: 0, width: a, height: size.height)), with: .color(Color.black.opacity(0.62)))
                    ctx.fill(Path(CGRect(x: b, y: 0, width: size.width - b, height: size.height)), with: .color(Color.black.opacity(0.62)))
                    // playhead
                    if model.status.playhead >= 0 && model.isPlaying {
                        let x = CGFloat(model.status.playhead) * size.width
                        ctx.fill(Path(CGRect(x: x - 1, y: 0, width: 2, height: size.height)), with: .color(.white))
                    }
                }
                .clipShape(RoundedRectangle(cornerRadius: 10))
                if !model.sampler.hasSample {
                    Text("DROP AUDIO OR TAP SAMPLE").font(.label(12)).foregroundColor(Neon.textDim).frame(width: w, height: h)
                }
                TrimMarker(x: CGFloat(model.project.trimStart) * w, height: h, isStart: true) { x in
                    model.setTrim(start: Float(x / w))
                } ended: { model.trimChangeEnded() }
                TrimMarker(x: CGFloat(model.project.trimEnd) * w, height: h, isStart: false) { x in
                    model.setTrim(end: Float(x / w))
                } ended: { model.trimChangeEnded() }
            }
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(Neon.edge, lineWidth: 1))
            .coordinateSpace(name: "wave")
        }
        .frame(height: 66)
        .accessibilityIdentifier("waveform")
    }
}

struct TrimMarker: View {
    let x: CGFloat
    let height: CGFloat
    let isStart: Bool
    let moved: (CGFloat) -> Void
    let ended: () -> Void
    var body: some View {
        ZStack {
            Rectangle().fill(Neon.yellow).frame(width: 2, height: height).shadow(color: Neon.yellow.opacity(0.9), radius: 4)
            Triangle().fill(Neon.yellow).frame(width: 16, height: 12)
                .rotationEffect(.degrees(isStart ? 90 : -90))
                .offset(x: isStart ? 8 : -8, y: -height / 2 + 8)
        }
        .frame(width: 34, height: height)
        .contentShape(Rectangle())
        .position(x: x, y: height / 2)
        .highPriorityGesture(
            DragGesture(minimumDistance: 0, coordinateSpace: .named("wave"))
                .onChanged { g in moved(g.location.x) }
                .onEnded { _ in ended() }
        )
        .accessibilityLabel(isStart ? "Trim start" : "Trim end")
    }
}

struct Triangle: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: r.midX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.maxY))
        p.closeSubpath()
        return p
    }
}

// MARK: - Step pads

struct StepPads: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        let cols = Array(repeating: GridItem(.flexible(), spacing: 6), count: 8)
        LazyVGrid(columns: cols, spacing: 6) {
            ForEach(0..<16, id: \.self) { i in
                PadView(index: i)
            }
        }
    }
}

struct PadView: View {
    @EnvironmentObject var model: AppModel
    let index: Int
    var body: some View {
        let s = model.step(index)
        let inRange = index < model.project.pattern.length
        let playing = model.isPlaying && model.status.step == index
        let color = Neon.stepColor(slice: s.slice)
        ZStack {
            RoundedRectangle(cornerRadius: 7)
                .fill(s.active ? color.opacity(inRange ? 0.95 : 0.35) : Neon.panel2.opacity(inRange ? 1 : 0.5))
            RoundedRectangle(cornerRadius: 7)
                .stroke(playing ? Color.white : (model.selectedStep == index ? Neon.text.opacity(0.8) : Neon.edge), lineWidth: playing ? 2.5 : 1)
            Text("\(index + 1)").font(.lcd(11)).foregroundColor(s.active ? Color.black.opacity(0.75) : Neon.textDim)
            if s.hasAnyLock {
                Circle().fill(s.active ? Color.black.opacity(0.7) : Neon.yellow).frame(width: 5, height: 5).offset(x: 14, y: -14)
            }
        }
        .frame(height: 42)
        .shadow(color: s.active && inRange ? color.opacity(0.55) : .clear, radius: 6)
        .contentShape(Rectangle())
        .onTapGesture {
            if model.mode == .perform { model.trigger(slice: s.slice % max(1, model.project.sliceCount)); model.selectedStep = index }
            else { model.toggleStep(index) }
        }
        .onLongPressGesture(minimumDuration: 0.35) {
            model.selectedStep = index
            model.sheet = .stepFx(index)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Step \(index + 1)")
        .accessibilityValue(s.active ? "on, slice \(s.slice + 1)" : "off")
        .accessibilityHint("Tap to toggle. Touch and hold to edit effects for this step.")
        .accessibilityAddTraits(.isButton)
    }
}

// MARK: - Mode tabs

struct ModeTabs: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        HStack(spacing: 6) {
            ForEach(MainMode.allCases) { m in
                Button {
                    model.mode = m
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: m.icon).font(.system(size: 13, weight: .bold))
                        Text(m.rawValue).font(.label(11))
                    }
                    .foregroundColor(model.mode == m ? .black : Neon.text)
                    .frame(maxWidth: .infinity).frame(height: 40)
                    .background(RoundedRectangle(cornerRadius: 10).fill(model.mode == m ? Neon.lime : Neon.panel2))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(model.mode == m ? Neon.lime : Neon.edge, lineWidth: 1))
                    .shadow(color: model.mode == m ? Neon.lime.opacity(0.6) : .clear, radius: 6)
                }
                .buttonStyle(.plain)
                .accessibilityIdentifier("mode-\(m.rawValue)")
                .accessibilityAddTraits(model.mode == m ? .isSelected : [])
            }
        }
    }
}

// MARK: - Knobs / mix

struct ControlArea: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        Group {
            if model.mode == .mix { MixPanel() } else { KnobGrid() }
        }
        .frame(height: 228)
    }
}

struct KnobGrid: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        let cols = Array(repeating: GridItem(.flexible(), spacing: 4), count: 3)
        LazyVGrid(columns: cols, spacing: 8) {
            ForEach(FxKind.allCases, id: \.self) { k in
                NeonKnob(fx: k, value: model.fxBinding(k, 0),
                         text: ParamText.text(k, 0, model.project.fx[k.rawValue].a),
                         defaultValue: Project.defaultFx[k.rawValue].a)
                    .accessibilityIdentifier("knob-\(k.title)")
            }
        }
    }
}

struct MixPanel: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        VStack(spacing: 10) {
            NeonSlider(title: "VOLUME", value: Binding(get: { model.project.volume / 1.5 }, set: { model.project.volume = $0 * 1.5 }),
                       valueText: "\(Int((model.project.volume * 100).rounded()))%", color: Neon.lime)
            NeonSlider(title: "PAN", value: Binding(get: { (model.project.pan + 1) / 2 }, set: { model.project.pan = $0 * 2 - 1 }),
                       valueText: abs(model.project.pan) < 0.02 ? "C" : (model.project.pan < 0 ? "L\(Int(-model.project.pan * 100))" : "R\(Int(model.project.pan * 100))"),
                       color: Neon.cyan)
            NeonSlider(title: "SWING", value: Binding(get: { model.project.pattern.swing }, set: { model.project.pattern.swing = $0 }),
                       valueText: ParamText.percent(model.project.pattern.swing), color: Neon.orange)
            NeonSlider(title: "PATTERN PROBABILITY", value: Binding(get: { model.project.pattern.probability }, set: { model.project.pattern.probability = $0 }),
                       valueText: ParamText.percent(model.project.pattern.probability), color: Neon.magenta)
        }
        .padding(14)
        .keyCap(color: Neon.panel, corner: 14)
    }
}

// MARK: - Transport

struct TransportBar: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        HStack(spacing: 8) {
            Button { model.sheet = .sample } label: {
                ZStack {
                    Circle().fill(Neon.red).frame(width: 50, height: 50).neonGlow(Neon.red, radius: 8, opacity: 0.6)
                    Text("REC").font(.chunky(12)).foregroundColor(.white)
                }
            }
            .buttonStyle(.plain).accessibilityLabel("Record or import a sample").accessibilityIdentifier("btn-rec")

            Button { model.togglePlay() } label: {
                Image(systemName: model.isPlaying ? "pause.fill" : "play.fill")
                    .font(.system(size: 22, weight: .bold)).foregroundColor(.black)
                    .frame(width: 58, height: 50)
                    .background(RoundedRectangle(cornerRadius: 12).fill(Neon.lime))
                    .neonGlow(Neon.lime, radius: 8, opacity: 0.6)
            }
            .buttonStyle(.plain).accessibilityLabel(model.isPlaying ? "Pause" : "Play").accessibilityIdentifier("btn-play")

            Button { model.stop() } label: {
                Image(systemName: "stop.fill")
                    .font(.system(size: 18, weight: .bold)).foregroundColor(.black)
                    .frame(width: 50, height: 50)
                    .background(RoundedRectangle(cornerRadius: 12).fill(Color.white.opacity(0.92)))
            }
            .buttonStyle(.plain).accessibilityLabel("Stop").accessibilityIdentifier("btn-stop")

            Button { model.selectedStep = model.selectedStep; model.sheet = .stepFx(model.selectedStep) } label: {
                VStack(spacing: 1) {
                    Image(systemName: "slider.horizontal.3").font(.system(size: 14, weight: .bold))
                    Text("STEP FX").font(.label(8))
                }
                .foregroundColor(Neon.yellow).frame(width: 52, height: 50).keyCap(corner: 10)
            }
            .buttonStyle(.plain).accessibilityLabel("Effects for the selected step").accessibilityIdentifier("btn-stepfx")

            Button { model.project.pattern.loop.toggle() } label: {
                VStack(spacing: 1) {
                    Image(systemName: "repeat").font(.system(size: 14, weight: .bold))
                    Text("LOOP").font(.label(8))
                }
                .foregroundColor(model.project.pattern.loop ? Neon.cyan : Neon.textDim).frame(width: 46, height: 50)
                .keyCap(lit: model.project.pattern.loop, litColor: Neon.cyan, corner: 10)
            }
            .buttonStyle(.plain).accessibilityLabel("Loop").accessibilityValue(model.project.pattern.loop ? "on" : "off")

            Button { model.tapTempo() } label: {
                VStack(spacing: 0) {
                    Text("\(Int(model.project.bpm.rounded()))").font(.lcd(15)).foregroundColor(Neon.orange)
                    Text("TAP").font(.label(9)).foregroundColor(Neon.textDim)
                }
                .frame(width: 44, height: 50).keyCap(corner: 10)
            }
            .buttonStyle(.plain).accessibilityLabel("Tap tempo").accessibilityValue("\(Int(model.project.bpm.rounded())) BPM").accessibilityIdentifier("btn-tap")
        }
        .frame(height: 54)
    }
}
