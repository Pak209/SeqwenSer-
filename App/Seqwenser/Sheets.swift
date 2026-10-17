import SwiftUI
import UniformTypeIdentifiers
import SeqwenserKit

struct SheetHost: View {
    @EnvironmentObject var model: AppModel
    let sheet: ActiveSheet
    var body: some View {
        ZStack {
            Neon.bg.ignoresSafeArea()
            ScrollView {
                Group {
                    switch sheet {
                    case .sample: SampleSheet()
                    case .pattern: PatternSheet()
                    case .stepFx: StepFxSheet(step: model.selectedStep)
                    case .projects: ProjectsSheet()
                    case .export: ExportSheet()
                    case .settings: AboutSheet()
                    }
                }
                .padding(18)
            }
        }
        .preferredColorScheme(.dark)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
    }
}

// MARK: - SAMPLE

enum SampleSource: String, CaseIterable { case record = "RECORD", importFile = "IMPORT", files = "FILES", mic = "MIC" }

struct SampleSheet: View {
    @EnvironmentObject var model: AppModel
    @State private var source: SampleSource = .record
    @State private var showImporter = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            SheetHeader(title: "SAMPLE", icon: "waveform", color: Neon.lime) { model.sheet = nil }
            HStack(spacing: 6) {
                ForEach(SampleSource.allCases, id: \.self) { s in
                    Pill(title: s.rawValue, selected: source == s) { source = s }
                        .accessibilityIdentifier("src-\(s.rawValue)")
                }
            }
            switch source {
            case .record, .mic: recordPanel
            case .importFile, .files: importPanel
            }
            VStack(spacing: 14) {
                NeonToggle(title: "AUTO SLICE", isOn: $model.project.autoSlice, color: Neon.lime)
                NeonSlider(title: "SENSITIVITY", value: $model.project.sensitivity,
                           valueText: ParamText.percent(model.project.sensitivity), color: Neon.lime) { editing in
                    if !editing && model.project.autoSlice { model.resliceNow() }
                }
                Text(model.project.autoSlice
                     ? "Finds the hits in your sample and cuts a slice at each one. Higher sensitivity finds more."
                     : "Auto slice is off: the sample is cut into 16 equal parts.")
                    .font(.label(11)).foregroundColor(Neon.textDim)
                HStack {
                    Text(model.sampleInfo).font(.lcd(11)).foregroundColor(Neon.cyan).lineLimit(2)
                    Spacer()
                    Button("RE-SLICE") { model.resliceNow() }.font(.label(11)).foregroundColor(Neon.yellow)
                }
            }
            .padding(14).keyCap(color: Neon.panel, corner: 14)
        }
        .fileImporter(isPresented: $showImporter, allowedContentTypes: [.audio], allowsMultipleSelection: false) { r in
            if case .success(let u) = r { model.importMany(u); model.sheet = nil }
            else if case .failure(let e) = r { model.message = "Could not open the file: \(e.localizedDescription)" }
        }
        .onChange(of: model.project.autoSlice) { _ in model.resliceNow() }
    }

    private var recordPanel: some View {
        VStack(spacing: 14) {
            HStack(alignment: .center, spacing: 14) {
                VStack(spacing: 6) {
                    MascotView(color: true).frame(width: 84, height: 84)
                    Text(model.isRecording ? "KEEP GOING!" : "MAKE SOME NOISE").font(.label(11)).foregroundColor(Neon.text)
                        .padding(.horizontal, 8).padding(.vertical, 5)
                        .background(RoundedRectangle(cornerRadius: 6).fill(Neon.panel2))
                        .overlay(RoundedRectangle(cornerRadius: 6).stroke(Neon.edge))
                }
                Button { model.toggleRecord() } label: {
                    ZStack {
                        Circle().fill(Neon.red.opacity(model.isRecording ? 1 : 0.85)).frame(width: 128, height: 128)
                            .neonGlow(Neon.red, radius: model.isRecording ? 16 : 10, opacity: 0.7)
                        Circle().stroke(Color.white.opacity(0.25), lineWidth: 3).frame(width: 112, height: 112)
                        VStack(spacing: 3) {
                            if model.isRecording {
                                Image(systemName: "stop.fill").font(.system(size: 26))
                                Text(String(format: "%.1f s", model.recordSeconds)).font(.lcd(15))
                            } else {
                                Text("TAP TO").font(.chunky(15)); Text("RECORD").font(.chunky(15))
                                Text("MAX 60 SEC | AUTO SLICE").font(.label(7))
                            }
                        }
                        .foregroundColor(.white)
                    }
                }
                .buttonStyle(.plain)
                .accessibilityLabel(model.isRecording ? "Stop recording" : "Tap to record")
                .accessibilityIdentifier("btn-record")
            }
            if model.isRecording {
                GeometryReader { g in
                    ZStack(alignment: .leading) {
                        Capsule().fill(Neon.track)
                        Capsule().fill(Neon.red).frame(width: g.size.width * CGFloat(min(1, model.recordLevel * 1.5)))
                    }
                }
                .frame(height: 8)
            }
            if model.micDenied {
                Text("Microphone access is off. Turn it on in Settings > Seqwenser > Microphone.")
                    .font(.label(12)).foregroundColor(Neon.yellow)
            }
        }
        .padding(14).keyCap(color: Neon.panel, corner: 14)
    }

    private var importPanel: some View {
        VStack(spacing: 12) {
            Text("Bring in any audio file: WAV, AIFF, MP3, M4A, FLAC, CAF. You can also drop a file onto the app, or use Share > Seqwenser from another app.")
                .font(.label(12)).foregroundColor(Neon.textDim).frame(maxWidth: .infinity, alignment: .leading)
            Button { showImporter = true } label: {
                Label("CHOOSE FILE…", systemImage: "folder.fill").font(.chunky(15)).foregroundColor(.black)
                    .frame(maxWidth: .infinity).frame(height: 50)
                    .background(RoundedRectangle(cornerRadius: 12).fill(Neon.cyan))
                    .neonGlow(Neon.cyan, radius: 8, opacity: 0.5)
            }
            .buttonStyle(.plain).accessibilityIdentifier("btn-choose-file")
            Button { model.loadDemoKit(); model.sheet = nil } label: {
                Label("LOAD DEMO KIT", systemImage: "music.note").font(.label(13)).foregroundColor(Neon.text)
                    .frame(maxWidth: .infinity).frame(height: 44).keyCap(corner: 12)
            }
            .buttonStyle(.plain)
        }
        .padding(14).keyCap(color: Neon.panel, corner: 14)
    }
}

// MARK: - STEP FX / PARAMS

struct StepFxSheet: View {
    @EnvironmentObject var model: AppModel
    let step: Int
    var body: some View {
        let s = model.step(step)
        VStack(alignment: .leading, spacing: 14) {
            SheetHeader(title: "STEP \(step + 1) FX / PARAMS", icon: "square.fill", color: Neon.yellow) { model.sheet = nil }
            // step navigation
            HStack {
                Button { model.selectedStep = (step + 15) % 16 } label: { Image(systemName: "chevron.left") }
                Spacer()
                Toggle("STEP ON", isOn: Binding(get: { s.active }, set: { var n = s; n.active = $0; model.setStep(step, n) }))
                    .toggleStyle(.switch).tint(Neon.lime).font(.label(12)).frame(width: 150)
                Spacer()
                Button { model.selectedStep = (step + 1) % 16 } label: { Image(systemName: "chevron.right") }
            }
            .foregroundColor(Neon.text)
            // effect tabs
            HStack(spacing: 6) {
                ForEach(FxKind.allCases, id: \.self) { k in
                    Button { model.fxTab = k } label: {
                        VStack(spacing: 2) {
                            Image(systemName: Neon.fxIcon(k)).font(.system(size: 18, weight: .bold))
                            if s.locks[k.rawValue] != nil { Circle().fill(Neon.fx(k)).frame(width: 5, height: 5) } else { Color.clear.frame(width: 5, height: 5) }
                        }
                        .foregroundColor(Neon.fx(k))
                        .frame(maxWidth: .infinity).frame(height: 50)
                        .background(RoundedRectangle(cornerRadius: 10).fill(model.fxTab == k ? Neon.fx(k).opacity(0.22) : Neon.panel2))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(model.fxTab == k ? Neon.fx(k) : Neon.edge, lineWidth: model.fxTab == k ? 2 : 1))
                        .shadow(color: model.fxTab == k ? Neon.fx(k).opacity(0.5) : .clear, radius: 6)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(k.title)
                    .accessibilityIdentifier("fxtab-\(k.title)")
                    .accessibilityAddTraits(model.fxTab == k ? .isSelected : [])
                }
            }
            let k = model.fxTab
            VStack(alignment: .leading, spacing: 14) {
                HStack {
                    Text(k.title).font(.chunky(16)).foregroundColor(Neon.fx(k))
                    Spacer()
                    if s.locks[k.rawValue] != nil {
                        Button("CLEAR LOCK") { var n = s; n.locks[k.rawValue] = nil; model.setStep(step, n) }
                            .font(.label(11)).foregroundColor(Neon.yellow)
                    } else {
                        Text("following global").font(.label(10)).foregroundColor(Neon.textDim)
                    }
                }
                HStack(alignment: .top, spacing: 14) {
                    VStack(spacing: 14) {
                        ForEach(0..<2, id: \.self) { p in
                            let b = model.lockBinding(step: step, fx: k, param: p)
                            NeonSlider(title: k.paramNames[p], value: b, valueText: ParamText.text(k, p, b.wrappedValue), color: Neon.fx(k))
                                .accessibilityIdentifier("slider-\(k.title)-\(p)")
                        }
                    }
                    if k == .pitch {
                        VStack(spacing: 8) {
                            NeonToggle(title: "REVERSE", isOn: flag(\.reverse), color: Neon.cyan)
                            NeonToggle(title: "LO-FI", isOn: flag(\.lofi), color: Neon.lime)
                            NeonToggle(title: "STRETCH", isOn: flag(\.stretch), color: Neon.orange)
                        }
                        .frame(width: 140)
                    }
                }
                Text(k.hint).font(.label(11)).foregroundColor(Neon.textDim)
            }
            .padding(14).keyCap(color: Neon.panel, corner: 14)

            // step properties
            VStack(alignment: .leading, spacing: 12) {
                Text("STEP").font(.label(11)).foregroundColor(Neon.textDim)
                HStack(spacing: 8) {
                    Text("SLICE").font(.label(11)).foregroundColor(Neon.textDim)
                    Stepper(value: Binding(get: { s.slice + 1 }, set: { var n = s; n.slice = max(0, min(15, $0 - 1)); model.setStep(step, n); model.trigger(slice: n.slice % max(1, model.project.sliceCount)) }), in: 1...16) {
                        Text("\(s.slice + 1) / \(max(1, model.project.sliceCount))").font(.lcd(13)).foregroundColor(Neon.stepColor(slice: s.slice))
                    }
                }
                NeonSlider(title: "VELOCITY", value: Binding(get: { s.velocity }, set: { var n = s; n.velocity = $0; model.setStep(step, n) }),
                           valueText: "\(Int((s.velocity * 127).rounded()))", color: Neon.lime)
                NeonSlider(title: "STEP PROBABILITY", value: Binding(get: { s.probability }, set: { var n = s; n.probability = $0; model.setStep(step, n) }),
                           valueText: ParamText.percent(s.probability), color: Neon.magenta)
                HStack(spacing: 10) {
                    DropdownChip(title: "CONDITION", options: StepCondition.allCases, selection: Binding(get: { s.condition }, set: { var n = s; n.condition = $0; model.setStep(step, n) }), label: { $0.title })
                    DropdownChip(title: "VELOCITY", options: VelocityCondition.allCases, selection: Binding(get: { s.velocityCondition }, set: { var n = s; n.velocityCondition = $0; model.setStep(step, n) }), label: { $0.title }, color: Neon.orange)
                }
            }
            .padding(14).keyCap(color: Neon.panel, corner: 14)
        }
    }

    private func flag(_ kp: WritableKeyPath<Step, Bool>) -> Binding<Bool> {
        Binding(get: { model.step(step)[keyPath: kp] }, set: { v in var n = model.step(step); n[keyPath: kp] = v; model.setStep(step, n) })
    }
}

// MARK: - PATTERN

struct PatternSheet: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                SheetHeader(title: "PATTERN", icon: "square.grid.4x3.fill", color: Neon.lime) { model.sheet = nil }
            }
            HStack {
                Text("BANK").font(.label(11)).foregroundColor(Neon.textDim)
                Spacer()
                ForEach(0..<2, id: \.self) { i in
                    let t = i == 0 ? "A" : "B"
                    Button { model.project.pattern.bank = i } label: {
                        Text(t).font(.chunky(15)).foregroundColor(model.project.pattern.bank == i ? .black : Neon.text)
                            .frame(width: 44, height: 36)
                            .background(RoundedRectangle(cornerRadius: 8).fill(model.project.pattern.bank == i ? Neon.lime : Neon.panel2))
                            .overlay(RoundedRectangle(cornerRadius: 8).stroke(Neon.edge))
                    }
                    .buttonStyle(.plain).accessibilityIdentifier("bank-\(t)")
                }
            }
            StepPads()
            Stepper(value: $model.project.pattern.length, in: 1...16) {
                Text("LENGTH  \(model.project.pattern.length) STEPS").font(.label(12)).foregroundColor(Neon.text)
            }
            VStack(alignment: .leading, spacing: 8) {
                Text("PLAY MODE").font(.label(11)).foregroundColor(Neon.textDim)
                HStack(spacing: 6) {
                    ForEach(PlayMode.allCases, id: \.self) { m in
                        Pill(title: m.title, selected: model.project.pattern.playMode == m) { model.project.pattern.playMode = m }
                            .accessibilityIdentifier("mode-\(m.title)")
                    }
                }
                Text("RATE").font(.label(11)).foregroundColor(Neon.textDim)
                HStack(spacing: 6) {
                    ForEach(StepRate.allCases, id: \.self) { r in
                        Pill(title: r.title, selected: model.project.pattern.rate == r) { model.project.pattern.rate = r }
                            .accessibilityIdentifier("rate-\(r.title)")
                    }
                }
            }
            NeonSlider(title: "PROBABILITY", value: $model.project.pattern.probability,
                       valueText: ParamText.percent(model.project.pattern.probability), color: Neon.lime)
            NeonSlider(title: "SWING", value: $model.project.pattern.swing,
                       valueText: ParamText.percent(model.project.pattern.swing), color: Neon.orange)
            HStack(spacing: 10) {
                DropdownChip(title: "CONDITION", options: StepCondition.allCases,
                             selection: Binding(get: { model.step(model.selectedStep).condition }, set: { var n = model.step(model.selectedStep); n.condition = $0; model.setStep(model.selectedStep, n) }),
                             label: { $0.title })
                DropdownChip(title: "VELOCITY", options: VelocityCondition.allCases,
                             selection: Binding(get: { model.step(model.selectedStep).velocityCondition }, set: { var n = model.step(model.selectedStep); n.velocityCondition = $0; model.setStep(model.selectedStep, n) }),
                             label: { $0.title }, color: Neon.orange)
            }
            Text("Conditions apply to the selected step (\(model.selectedStep + 1)).").font(.label(11)).foregroundColor(Neon.textDim)
            HStack(spacing: 10) {
                Button { model.fillFromSlices() } label: { Text("FILL FROM SLICES").font(.label(12)).foregroundColor(Neon.text).frame(maxWidth: .infinity).frame(height: 40).keyCap(corner: 10) }.buttonStyle(.plain)
                Button { model.clearPattern() } label: { Text("CLEAR").font(.label(12)).foregroundColor(Neon.red).frame(maxWidth: .infinity).frame(height: 40).keyCap(corner: 10) }.buttonStyle(.plain)
            }
        }
    }
}

// MARK: - PROJECTS

struct ProjectsSheet: View {
    @EnvironmentObject var model: AppModel
    @State private var name = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            SheetHeader(title: "PROJECTS", icon: "folder.fill", color: Neon.cyan) { model.sheet = nil }
            HStack {
                TextField("Project name", text: $name)
                    .textFieldStyle(.roundedBorder)
                    .onAppear { name = model.project.name }
                Button {
                    model.project.name = name
                    model.saveProject()
                } label: {
                    Text("SAVE").font(.chunky(13)).foregroundColor(.black).padding(.horizontal, 18).frame(height: 36)
                        .background(RoundedRectangle(cornerRadius: 8).fill(Neon.lime))
                }.buttonStyle(.plain).accessibilityIdentifier("btn-save")
            }
            if model.savedProjects.isEmpty {
                Text("No saved projects yet.").font(.label(13)).foregroundColor(Neon.textDim)
            }
            ForEach(model.savedProjects, id: \.self) { n in
                HStack {
                    Button { model.loadProject(n) } label: {
                        HStack { Image(systemName: "doc.fill").foregroundColor(Neon.cyan); Text(n).font(.label(14)).foregroundColor(Neon.text); Spacer() }
                            .padding(12).keyCap(corner: 10)
                    }.buttonStyle(.plain)
                    Button { model.deleteProject(n) } label: {
                        Image(systemName: "trash").foregroundColor(Neon.red).frame(width: 40, height: 40).keyCap(corner: 10)
                    }.buttonStyle(.plain).accessibilityLabel("Delete \(n)")
                }
            }
        }
    }
}

// MARK: - EXPORT

struct ExportSheet: View {
    @EnvironmentObject var model: AppModel
    @State private var passes = 2
    @State private var bits24 = true
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            SheetHeader(title: "EXPORT BOUNCE", icon: "square.and.arrow.up", color: Neon.orange) { model.sheet = nil }
            Text("Renders the current pattern with all effects to a WAV file, then opens the share sheet (Files, AirDrop, other apps).")
                .font(.label(12)).foregroundColor(Neon.textDim)
            Stepper(value: $passes, in: 1...16) { Text("PASSES  \(passes)").font(.label(13)).foregroundColor(Neon.text) }
            NeonToggle(title: "24-BIT (OFF = 16-BIT)", isOn: $bits24, color: Neon.orange)
            Button { model.exportBounce(passes: passes, bits: bits24 ? 24 : 16) } label: {
                Text("RENDER").font(.chunky(15)).foregroundColor(.black).frame(maxWidth: .infinity).frame(height: 48)
                    .background(RoundedRectangle(cornerRadius: 12).fill(Neon.orange))
            }.buttonStyle(.plain).accessibilityIdentifier("btn-render")
            if let url = model.exportURL {
                ShareLink(item: url) {
                    Label("SHARE \(url.lastPathComponent)", systemImage: "square.and.arrow.up").font(.label(13)).foregroundColor(Neon.text)
                        .frame(maxWidth: .infinity).frame(height: 44).keyCap(corner: 12)
                }
            }
        }
    }
}

struct AboutSheet: View {
    @EnvironmentObject var model: AppModel
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            SheetHeader(title: "ABOUT", icon: "info.circle", color: Neon.purple) { model.sheet = nil }
            HStack(spacing: 12) {
                MascotView(color: true).frame(width: 60, height: 60)
                VStack(alignment: .leading) {
                    Text("Seqwenser").font(.chunky(20)).foregroundColor(Neon.lime)
                    Text("Sample, slice, sequence, twist.").font(.label(12)).foregroundColor(Neon.textDim)
                }
            }
            Text("Version 0.1.0. Open source under the GNU AGPL v3.0. Made with a shared C++ audio engine. Nothing in this app claims that the audio you load is cleared for release: that is up to you.")
                .font(.label(12)).foregroundColor(Neon.textDim)
            Text("Tips").font(.label(12)).foregroundColor(Neon.text)
            Text("• Tap a pad to switch a step on.\n• Touch and hold a pad to edit that step's effects (parameter locks).\n• Drag the yellow markers to trim.\n• Double-tap a knob to reset it.")
                .font(.label(12)).foregroundColor(Neon.textDim)
        }
    }
}
