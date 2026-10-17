import SwiftUI
import SeqwenserKit

/// Big neon-ring knob: drag up/down, double-tap to reset.
struct NeonKnob: View {
    let fx: FxKind
    @Binding var value: Float
    let text: String
    var defaultValue: Float = 0.5
    var onEditingChanged: (Bool) -> Void = { _ in }
    @State private var startValue: Float?

    var body: some View {
        let color = Neon.fx(fx)
        VStack(spacing: 3) {
            ZStack {
                Circle()
                    .trim(from: 0, to: 0.75)
                    .stroke(Neon.track, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                    .rotationEffect(.degrees(135))
                Circle()
                    .trim(from: 0, to: 0.75 * CGFloat(value.clamped(0, 1)))
                    .stroke(color, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                    .rotationEffect(.degrees(135))
                    .shadow(color: color.opacity(0.85), radius: 7)
                Circle()
                    .fill(RadialGradient(colors: [Color(hex: 0x2A3039), Color(hex: 0x101317)], center: .topLeading, startRadius: 2, endRadius: 46))
                    .padding(11)
                    .overlay(Circle().stroke(Neon.edge, lineWidth: 1).padding(11))
                Rectangle()
                    .fill(color)
                    .frame(width: 3, height: 11)
                    .offset(y: -25)
                    .rotationEffect(.degrees(Double(-135 + 270 * value.clamped(0, 1))))
                    .shadow(color: color.opacity(0.9), radius: 3)
                Image(systemName: Neon.fxIcon(fx))
                    .font(.system(size: 17, weight: .bold))
                    .foregroundColor(color)
            }
            .frame(width: 76, height: 76)
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if startValue == nil { startValue = value; onEditingChanged(true) }
                        let v = (startValue ?? value) - Float(g.translation.height) / 180
                        value = v.clamped(0, 1)
                    }
                    .onEnded { _ in startValue = nil; onEditingChanged(false) }
            )
            .simultaneousGesture(TapGesture(count: 2).onEnded { value = defaultValue })
            Text(fx.title)
                .font(.label(11))
                .foregroundColor(color)
            Text(text)
                .font(.lcd(11))
                .foregroundColor(Neon.textDim)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(fx.title)
        .accessibilityValue(text)
        .accessibilityAdjustableAction { dir in
            switch dir {
            case .increment: value = (value + 0.05).clamped(0, 1)
            case .decrement: value = (value - 0.05).clamped(0, 1)
            @unknown default: break
            }
        }
    }
}

/// Horizontal neon slider with a title and value read-out.
struct NeonSlider: View {
    let title: String
    @Binding var value: Float          // 0...1
    let valueText: String
    var color: Color = Neon.lime
    var onEditingChanged: (Bool) -> Void = { _ in }
    @State private var editing = false

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title).font(.label(11)).foregroundColor(Neon.textDim)
                Spacer()
                Text(valueText).font(.lcd(12)).foregroundColor(color)
            }
            GeometryReader { geo in
                let w = geo.size.width
                let x = CGFloat(value.clamped(0, 1)) * w
                ZStack(alignment: .leading) {
                    Capsule().fill(Neon.track).frame(height: 8)
                    Capsule().fill(color).frame(width: max(8, x), height: 8).shadow(color: color.opacity(0.7), radius: 5)
                    Circle().fill(Color.white).frame(width: 20, height: 20)
                        .overlay(Circle().stroke(color, lineWidth: 2))
                        .offset(x: min(max(0, x - 10), w - 20))
                }
                .frame(height: 24)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { g in
                            if !editing { editing = true; onEditingChanged(true) }
                            value = Float(g.location.x / max(1, w)).clamped(0, 1)
                        }
                        .onEnded { _ in editing = false; onEditingChanged(false) }
                )
            }
            .frame(height: 24)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(title)
        .accessibilityValue(valueText)
        .accessibilityAdjustableAction { dir in
            switch dir {
            case .increment: value = (value + 0.05).clamped(0, 1)
            case .decrement: value = (value - 0.05).clamped(0, 1)
            @unknown default: break
            }
        }
    }
}

struct NeonToggle: View {
    let title: String
    @Binding var isOn: Bool
    var color: Color = Neon.lime
    var body: some View {
        Button { isOn.toggle() } label: {
            HStack(spacing: 8) {
                Text(title).font(.label(12)).foregroundColor(isOn ? Neon.text : Neon.textDim)
                Spacer(minLength: 4)
                ZStack(alignment: isOn ? .trailing : .leading) {
                    Capsule().fill(isOn ? color.opacity(0.35) : Neon.track).frame(width: 38, height: 20)
                    Circle().fill(isOn ? color : Neon.textDim).frame(width: 16, height: 16).padding(2)
                        .shadow(color: isOn ? color.opacity(0.9) : .clear, radius: 4)
                }
            }
            .padding(.horizontal, 10).padding(.vertical, 8)
            .keyCap(color: Neon.panel, lit: isOn, litColor: color, corner: 10)
        }
        .buttonStyle(.plain)
        .accessibilityLabel(title)
        .accessibilityValue(isOn ? "on" : "off")
    }
}

/// Pill button used for tabs and choices.
struct Pill: View {
    let title: String
    var selected = false
    var color: Color = Neon.lime
    var action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.label(12))
                .foregroundColor(selected ? Color.black : Neon.text)
                .padding(.horizontal, 12).padding(.vertical, 8)
                .frame(maxWidth: .infinity)
                .background(Capsule().fill(selected ? color : Neon.panel2))
                .overlay(Capsule().stroke(selected ? color : Neon.edge, lineWidth: 1))
                .shadow(color: selected ? color.opacity(0.6) : .clear, radius: 6)
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }
}

/// Dropdown chip (Menu + Picker).
struct DropdownChip<T: Hashable>: View {
    let title: String
    let options: [T]
    @Binding var selection: T
    let label: (T) -> String
    var color: Color = Neon.cyan
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.label(10)).foregroundColor(Neon.textDim)
            Menu {
                ForEach(options, id: \.self) { o in
                    Button(label(o)) { selection = o }
                }
            } label: {
                HStack {
                    Text(label(selection)).font(.label(13)).foregroundColor(color)
                    Spacer()
                    Image(systemName: "chevron.down").font(.system(size: 11, weight: .bold)).foregroundColor(Neon.textDim)
                }
                .padding(.horizontal, 12).padding(.vertical, 10)
                .keyCap(color: Neon.panel, corner: 10)
            }
        }
    }
}

struct SheetHeader: View {
    let title: String
    var icon: String
    var color: Color = Neon.lime
    var onClose: () -> Void
    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: icon).foregroundColor(color).font(.system(size: 16, weight: .bold))
            Text(title).font(.chunky(18)).foregroundColor(color).neonGlow(color, radius: 5, opacity: 0.5)
            Spacer()
            Button(action: onClose) {
                Image(systemName: "xmark").font(.system(size: 14, weight: .bold)).foregroundColor(Neon.text)
                    .frame(width: 34, height: 34).keyCap(corner: 17)
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Close")
        }
    }
}
