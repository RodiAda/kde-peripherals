import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid
import org.kde.plasma.workspace.dbus as DBus

PlasmoidItem {
    id: root

    readonly property string service: "io.github.rodiada.Peripherals"
    readonly property string objectPath: "/io/github/rodiada/Peripherals"

    property string devicesJson: "[]"
    readonly property string kind: Plasmoid.metaData.pluginId.endsWith(".keyboard") ? "keyboard" : "mouse"
    readonly property bool daemonUp: watcher.registered
    readonly property var devices: daemonUp ? parseJson(devicesJson, []).filter(d => d.kind === kind) : []
    property string selectedId: ""
    readonly property var current: devices.find(d => d.id === selectedId) || devices[0] || null
    readonly property var sections: ["performance", "sensor", "buttons", "lighting", "keys", "power", "other"]
    readonly property var lowBattery: devices.filter(d => d.battery !== undefined && d.battery <= 15 && !d.charging)

    function parseJson(text, fallback) {
        if (!text)
            return fallback
        try {
            return JSON.parse(text)
        } catch (e) {
            return fallback
        }
    }

    function unwrap(v, fallback) {
        if (v !== null && typeof v === "object" && "value" in v)
            v = v.value
        return v === undefined || v === null ? fallback : v
    }

    function refresh() {
        const reply = DBus.SessionBus.asyncCall({
            service: root.service,
            path: root.objectPath,
            iface: "org.freedesktop.DBus.Properties",
            member: "GetAll",
            arguments: [new DBus.string(root.service)]
        })
        reply.finished.connect(() => {
            if (reply.isError) {
                console.warn("peripherals: GetAll", reply.error.message)
                return
            }
            root.devicesJson = unwrap(reply.value.Devices, "[]")
        })
    }

    function apply(device, change) {
        const reply = DBus.SessionBus.asyncCall({
            service: root.service,
            path: root.objectPath,
            iface: root.service,
            member: "Apply",
            arguments: [new DBus.string(device.id), new DBus.string(JSON.stringify(change))]
        })
        reply.finished.connect(() => {
            if (reply.isError)
                console.warn("peripherals: Apply", reply.error.message)
        })
    }

    function applyKey(device, key, value) {
        const change = {}
        change[key] = value
        apply(device, change)
    }

    Component.onCompleted: refresh()

    DBus.DBusServiceWatcher {
        id: watcher
        busType: DBus.BusType.Session
        watchedService: root.service
        onRegisteredChanged: {
            root.devicesJson = "[]"
            if (registered)
                root.refresh()
        }
    }

    DBus.Properties {
        busType: DBus.BusType.Session
        service: root.service
        path: root.objectPath
        iface: root.service
        onPropertiesChanged: (interfaceName, changed, invalidated) => {
            if (interfaceName === root.service && changed.Devices !== undefined)
                root.devicesJson = root.unwrap(changed.Devices, "[]")
        }
    }

    function kindIcon(kind) {
        return kind === "keyboard" ? "input-keyboard" : "input-mouse"
    }

    function batteryIcon(level, charging) {
        if (level === undefined || level < 0)
            return "battery-missing"
        const step = Math.min(100, Math.round(level / 10) * 10)
        return "battery-" + String(step).padStart(3, "0") + (charging ? "-charging" : "")
    }

    function transportLabel(t) {
        switch (t) {
        case "usb": return i18n("Cable")
        case "dongle": return i18n("Receiver")
        case "bluetooth": return i18n("Bluetooth")
        }
        return ""
    }

    function sectionLabel(s) {
        switch (s) {
        case "performance": return i18n("Performance")
        case "sensor": return i18n("Sensor")
        case "buttons": return i18n("Buttons")
        case "lighting": return i18n("Lighting")
        case "keys": return i18n("Keys")
        case "power": return i18n("Power")
        }
        return i18n("Other")
    }

    function unitText(u, n) {
        switch (u) {
        case "Hz": return i18nc("unit", "%1 Hz", n)
        case "mm": return i18nc("unit", "%1 mm", n)
        case "ms": return i18nc("unit", "%1 ms", n)
        case "s": return i18nc("unit", "%1 s", n)
        case "min": return i18nc("unit", "%1 min", n)
        case "deg": return i18nc("unit", "%1°", n)
        case "%": return i18nc("unit", "%1%", n)
        }
        return String(n)
    }

    function tr(text) {
        switch (text) {
        case "DPI": return i18n("DPI")
        case "Polling rate": return i18n("Polling rate")
        case "Lift-off distance": return i18n("Lift-off distance")
        case "Sensor mode": return i18n("Sensor mode")
        case "Basic": return i18n("Basic")
        case "Competitive": return i18n("Competitive")
        case "Competitive MAX": return i18n("Competitive MAX")
        case "Motion sync": return i18n("Motion sync")
        case "Angle snapping": return i18n("Angle snapping")
        case "Ripple control": return i18n("Ripple control")
        case "Sensor angle": return i18n("Sensor angle")
        case "Debounce time": return i18n("Debounce time")
        case "Left button": return i18n("Left button")
        case "Right button": return i18n("Right button")
        case "Middle button": return i18n("Middle button")
        case "Back button": return i18n("Back button")
        case "Forward button": return i18n("Forward button")
        case "Left click": return i18n("Left click")
        case "Right click": return i18n("Right click")
        case "Middle click": return i18n("Middle click")
        case "Back": return i18n("Back")
        case "Forward": return i18n("Forward")
        case "DPI cycle": return i18n("DPI cycle")
        case "DPI up": return i18n("DPI up")
        case "DPI down": return i18n("DPI down")
        case "Scroll up": return i18n("Scroll up")
        case "Scroll down": return i18n("Scroll down")
        case "Scroll left": return i18n("Scroll left")
        case "Scroll right": return i18n("Scroll right")
        case "Disabled": return i18n("Disabled")
        case "Custom": return i18n("Custom")
        case "Sleep after": return i18n("Sleep after")
        case "Long-distance mode": return i18n("Long-distance mode")
        case "DPI indicator": return i18n("DPI indicator")
        case "Effect": return i18n("Effect")
        case "Steady": return i18n("Steady")
        case "Breathing": return i18n("Breathing")
        case "Brightness": return i18n("Brightness")
        case "Speed": return i18n("Speed")
        case "Low": return i18n("Low")
        case "Medium": return i18n("Medium")
        case "High": return i18n("High")
        case "Slow": return i18n("Slow")
        case "Fast": return i18n("Fast")
        case "Off": return i18n("Off")
        case "On": return i18n("On")
        case "Color": return i18n("Color")
        case "Direction": return i18n("Direction")
        case "Mode": return i18n("Mode")
        case "Never": return i18n("Never")
        case "Rainbow colors": return i18n("Rainbow colors")
        case "Lighting off after": return i18n("Lighting off after")
        case "Neon Stream": return i18n("Neon Stream")
        case "Ripples Shining": return i18n("Ripples Shining")
        case "Rotating Windmill": return i18n("Rotating Windmill")
        case "Sine Wave": return i18n("Sine Wave")
        case "Rainbow Roulette": return i18n("Rainbow Roulette")
        case "Stars Twinkle": return i18n("Stars Twinkle")
        case "Layer Upon Layer": return i18n("Layer Upon Layer")
        case "Rich and Honored": return i18n("Rich and Honored")
        case "Marquee Effect": return i18n("Marquee Effect")
        case "Rotating Storm": return i18n("Rotating Storm")
        case "Serpentine Horse Race": return i18n("Serpentine Horse Race")
        case "Retro Snake": return i18n("Retro Snake")
        case "Diagonal Transformation": return i18n("Diagonal Transformation")
        case "Ambilight": return i18n("Ambilight")
        case "Streamer": return i18n("Streamer")
        case "Neon": return i18n("Neon")
        case "Shadow Disappear": return i18n("Shadow Disappear")
        case "Flash Away": return i18n("Flash Away")
        case "Scroll wheel": return i18n("Scroll wheel")
        case "Ratchet": return i18n("Ratchet")
        case "Free spin": return i18n("Free spin")
        case "SmartShift": return i18n("SmartShift")
        case "SmartShift sensitivity": return i18n("SmartShift sensitivity")
        case "High-resolution scrolling": return i18n("High-resolution scrolling")
        case "Natural scrolling": return i18n("Natural scrolling")
        case "Media keys without Fn": return i18n("Media keys without Fn")
        case "Easy-Switch channel": return i18n("Easy-Switch channel")
        case "Device is asleep": return i18n("Device is asleep")
        case "Unsupported polling rate": return i18n("Unsupported polling rate")
        case "Unsupported lift-off distance": return i18n("Unsupported lift-off distance")
        case "Invalid DPI stages": return i18n("Invalid DPI stages")
        case "Invalid button action": return i18n("Invalid button action")
        case "Mouse is asleep": return i18n("Mouse is asleep")
        case "Write failed": return i18n("Write failed")
        case "Invalid value": return i18n("Invalid value")
        case "At least one button must keep the left click": return i18n("At least one button must keep the left click")
        }
        const channel = /^Channel (\d+)$/.exec(text)
        if (channel)
            return i18n("Channel %1", channel[1])
        return text
    }

    function optionText(control, opt) {
        if (opt.label !== undefined)
            return tr(opt.label)
        const n = opt.number !== undefined ? opt.number : opt.value
        return unitText(opt.unit || control.unit || "", n)
    }

    function deviceSummary(d) {
        const parts = []
        if (d.online === false)
            parts.push(i18n("asleep"))
        if (d.battery !== undefined)
            parts.push(d.charging ? i18n("%1%, charging", d.battery) : i18n("%1%", d.battery))
        return parts.join(" · ")
    }

    Plasmoid.icon: kindIcon(kind)
    Plasmoid.status: lowBattery.length > 0 ? PlasmaCore.Types.NeedsAttentionStatus
        : devices.length > 0 || expanded ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.HiddenStatus

    toolTipMainText: kind === "keyboard" ? i18n("Keyboard") : i18n("Mouse")
    toolTipSubText: {
        if (!daemonUp)
            return i18n("peripheralsd is not running")
        if (devices.length === 0)
            return kind === "keyboard" ? i18n("No supported keyboard") : i18n("No supported mouse")
        return devices.map(d => {
            const s = deviceSummary(d)
            return s.length > 0 ? d.name + ": " + s : d.name
        }).join("\n")
    }

    component HeldSwitch: RowLayout {
        id: held
        property string text
        property bool checked
        property bool enabledSwitch: true
        signal toggled(bool on)
        Layout.fillWidth: true
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: held.text
            wrapMode: Text.WordWrap
        }
        PlasmaComponents3.Switch {
            id: sw
            enabled: held.enabledSwitch
            checked: held.checked
            onToggled: {
                held.toggled(checked)
                hold.restart()
            }
            Timer {
                id: hold
                interval: 3000
                onTriggered: sw.checked = Qt.binding(() => held.checked)
            }
        }
    }

    component ChoiceRow: RowLayout {
        id: choiceRow
        property var control
        property string pending: ""
        property bool hasPending: false
        signal chosen(var value)
        Layout.fillWidth: true
        readonly property var shown: hasPending ? pendingValue : control.value
        property var pendingValue
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: root.tr(choiceRow.control.label)
            wrapMode: Text.WordWrap
        }
        PlasmaComponents3.ComboBox {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 9
            model: choiceRow.control.options.map(o => root.optionText(choiceRow.control, o))
            currentIndex: choiceRow.control.options.findIndex(o => o.value === choiceRow.shown)
            onActivated: index => {
                const v = choiceRow.control.options[index].value
                choiceRow.pendingValue = v
                choiceRow.hasPending = true
                choiceHold.restart()
                choiceRow.chosen(v)
            }
        }
        Timer {
            id: choiceHold
            interval: 3000
            onTriggered: choiceRow.hasPending = false
        }
    }

    component SliderRow: ColumnLayout {
        id: sliderRow
        property var control
        signal moved(int value)
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: root.tr(sliderRow.control.label)
            }
            PlasmaComponents3.Label {
                text: root.unitText(sliderRow.control.unit || "", Math.round(slider.value / slider.step) * slider.step)
                opacity: 0.7
            }
        }
        PlasmaComponents3.Slider {
            id: slider
            Layout.fillWidth: true
            readonly property int step: sliderRow.control.step || 1
            from: sliderRow.control.min
            to: sliderRow.control.max
            value: sliderRow.control.value
            onMoved: sliderCommit.restart()
            Timer {
                id: sliderCommit
                interval: 400
                onTriggered: sliderRow.moved(Math.round(slider.value / slider.step) * slider.step)
            }
        }
    }

    component ColorRow: RowLayout {
        id: colorRow
        property var control
        signal picked(string value)
        Layout.fillWidth: true
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: root.tr(colorRow.control.label)
        }
        PlasmaComponents3.Button {
            implicitWidth: Kirigami.Units.gridUnit * 3
            contentItem: Rectangle {
                radius: Kirigami.Units.cornerRadius
                color: colorRow.control.value
                border.color: Kirigami.Theme.textColor
                border.width: 1
                opacity: 0.9
            }
            onClicked: dialog.open()
        }
        ColorDialog {
            id: dialog
            selectedColor: colorRow.control.value
            onAccepted: colorRow.picked(selectedColor.toString())
        }
    }

    component DpiEditor: ColumnLayout {
        id: dpiEditor
        property var control
        property var device
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        property var stages: control.stages.slice()
        property int active: control.active
        onControlChanged: {
            stages = control.stages.slice()
            active = control.active
        }
        function commit() {
            root.applyKey(device, "dpi", { stages: stages, active: Math.min(active, stages.length - 1) })
        }
        PlasmaComponents3.Label {
            text: i18n("DPI stages")
        }
        Repeater {
            model: dpiEditor.stages.length
            delegate: RowLayout {
                required property int index
                Layout.fillWidth: true
                PlasmaComponents3.RadioButton {
                    checked: index === dpiEditor.active
                    onToggled: {
                        if (checked) {
                            dpiEditor.active = index
                            root.applyKey(dpiEditor.device, "dpiActive", index)
                        }
                    }
                }
                Rectangle {
                    visible: !!dpiEditor.control.colors
                    width: Kirigami.Units.gridUnit * 0.6
                    height: width
                    radius: width / 2
                    color: dpiEditor.control.colors ? dpiEditor.control.colors[index] || "transparent" : "transparent"
                }
                PlasmaComponents3.SpinBox {
                    Layout.fillWidth: true
                    from: dpiEditor.control.min
                    to: dpiEditor.control.max
                    stepSize: dpiEditor.control.step
                    editable: true
                    value: dpiEditor.stages[index]
                    onValueModified: {
                        const next = dpiEditor.stages.slice()
                        next[index] = value
                        dpiEditor.stages = next
                        dpiCommit.restart()
                    }
                }
                PlasmaComponents3.ToolButton {
                    icon.name: "list-remove"
                    enabled: dpiEditor.stages.length > 1
                    onClicked: {
                        const next = dpiEditor.stages.slice()
                        next.splice(index, 1)
                        dpiEditor.stages = next
                        if (dpiEditor.active >= next.length)
                            dpiEditor.active = next.length - 1
                        dpiEditor.commit()
                    }
                    PlasmaComponents3.ToolTip.text: i18n("Remove stage")
                    PlasmaComponents3.ToolTip.visible: hovered
                }
            }
        }
        PlasmaComponents3.Button {
            visible: dpiEditor.stages.length < dpiEditor.control.maxStages
            icon.name: "list-add"
            text: i18n("Add stage")
            onClicked: {
                const next = dpiEditor.stages.slice()
                next.push(Math.min(next[next.length - 1] * 2, dpiEditor.control.max))
                dpiEditor.stages = next
                dpiEditor.commit()
            }
        }
        Timer {
            id: dpiCommit
            interval: 800
            onTriggered: dpiEditor.commit()
        }
    }

    compactRepresentation: MouseArea {
        hoverEnabled: true
        property bool wasExpanded: false
        onPressed: wasExpanded = root.expanded
        onClicked: root.expanded = !wasExpanded

        Kirigami.Icon {
            anchors.fill: parent
            source: Plasmoid.icon
            active: parent.containsMouse
            opacity: root.devices.length > 0 ? 1 : 0.5
        }

        Rectangle {
            visible: root.lowBattery.length > 0
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: lowLabel.implicitWidth + Kirigami.Units.smallSpacing
            height: lowLabel.implicitHeight
            radius: height / 2
            color: Kirigami.Theme.negativeTextColor
            PlasmaComponents3.Label {
                id: lowLabel
                anchors.centerIn: parent
                text: root.lowBattery.length > 0 ? String(root.lowBattery[0].battery) : ""
                font.pixelSize: Math.max(Kirigami.Theme.smallFont.pixelSize * 0.8, 7)
                font.bold: true
                color: "white"
            }
        }
    }

    fullRepresentation: PlasmaExtras.Representation {
        readonly property real contentFitHeight: Math.min(
            heading.implicitHeight + content.implicitHeight + Kirigami.Units.largeSpacing * 2,
            Screen.desktopAvailableHeight * 0.8)
        Layout.minimumWidth: Kirigami.Units.gridUnit * 22
        Layout.preferredWidth: Layout.minimumWidth
        Layout.minimumHeight: root.current ? contentFitHeight : placeholder.implicitHeight + Kirigami.Units.gridUnit * 2
        Layout.preferredHeight: Layout.minimumHeight
        Layout.maximumHeight: Layout.minimumHeight

        header: PlasmaExtras.PlasmoidHeading {
            id: heading
            visible: root.devices.length > 0
            ColumnLayout {
                anchors.fill: parent
                spacing: Kirigami.Units.smallSpacing
                PlasmaComponents3.TabBar {
                    Layout.fillWidth: true
                    visible: root.devices.length > 1
                    currentIndex: Math.max(0, root.devices.findIndex(d => root.current && d.id === root.current.id))
                    Repeater {
                        model: root.devices
                        delegate: PlasmaComponents3.TabButton {
                            required property var modelData
                            icon.name: root.kindIcon(modelData.kind)
                            text: modelData.name
                            onClicked: root.selectedId = modelData.id
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: !!root.current
                    Kirigami.Icon {
                        source: root.current ? root.kindIcon(root.current.kind) : ""
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: implicitWidth
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 3
                            text: root.current ? root.current.name : ""
                            elide: Text.ElideRight
                        }
                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            text: root.current ? [root.current.vendor || "", root.transportLabel(root.current.transport),
                                                  root.current.firmware ? i18n("firmware %1", root.current.firmware) : ""]
                                                  .filter(s => s.length > 0).join(" · ") : ""
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            elide: Text.ElideRight
                        }
                    }
                    Kirigami.Icon {
                        visible: !!root.current && root.current.battery !== undefined
                        source: root.current ? root.batteryIcon(root.current.battery, root.current.charging) : ""
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: implicitWidth
                    }
                    PlasmaComponents3.Label {
                        visible: !!root.current && root.current.battery !== undefined
                        text: root.current && root.current.battery !== undefined ? i18n("%1%", root.current.battery) : ""
                    }
                }
            }
        }

        PlasmaExtras.PlaceholderMessage {
            id: placeholder
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 2
            visible: !root.current
            iconName: root.kindIcon(root.kind)
            text: root.daemonUp ? (root.kind === "keyboard" ? i18n("No supported keyboard") : i18n("No supported mouse"))
                                : i18n("Service not running")
            explanation: root.daemonUp
                ? i18n("Connect a supported device. Access to it is granted by the udev rule installed with this widget.")
                : i18n("Start it with:\nsystemctl --user start peripheralsd")
        }

        PlasmaComponents3.ScrollView {
            id: scroll
            anchors.fill: parent
            visible: !!root.current
            contentWidth: availableWidth
            contentHeight: content.implicitHeight + Kirigami.Units.largeSpacing * 2
            PlasmaComponents3.ScrollBar.horizontal.policy: PlasmaComponents3.ScrollBar.AlwaysOff

            ColumnLayout {
                id: content
                x: Kirigami.Units.largeSpacing
                y: Kirigami.Units.largeSpacing
                width: scroll.availableWidth - Kirigami.Units.largeSpacing * 2
                spacing: Kirigami.Units.largeSpacing
                readonly property var device: root.current
                readonly property var controls: device && device.controls ? device.controls : []

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !!content.device && content.device.untested === true
                    type: Kirigami.MessageType.Information
                    text: i18n("Support for this device has not been tested on real hardware yet.")
                    actions: [
                        Kirigami.Action {
                            text: i18n("Report how it works")
                            icon.name: "tools-report-bug"
                            onTriggered: Qt.openUrlExternally("https://github.com/RodiAda/kde-peripherals/issues")
                        }
                    ]
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !!content.device && content.device.online === false
                    type: Kirigami.MessageType.Information
                    text: content.device && content.device.kind === "keyboard"
                        ? i18n("The keyboard is asleep or out of range. Press any key to wake it up.")
                        : i18n("The mouse is asleep or out of range. Move it to wake it up.")
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !!content.device && content.device.writeOnly === true
                    type: Kirigami.MessageType.Information
                    text: content.device && content.device.settingsKnown
                        ? i18n("This keyboard cannot report its settings. The values below are the last ones set from this widget; changes made with Fn shortcuts are not shown.")
                        : i18n("This keyboard cannot report its settings. The values below are defaults until you change something here.")
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: !!content.device && !!content.device.error
                    type: Kirigami.MessageType.Warning
                    text: content.device && content.device.error ? root.tr(content.device.error) : ""
                }

                Repeater {
                    model: root.sections.filter(s => content.controls.some(c => (c.section || "other") === s))
                    delegate: ColumnLayout {
                        id: sectionBlock
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading {
                            level: 4
                            text: root.sectionLabel(sectionBlock.modelData)
                        }
                        Repeater {
                            model: content.controls.filter(c => (c.section || "other") === sectionBlock.modelData)
                            delegate: Loader {
                                id: loader
                                required property var modelData
                                Layout.fillWidth: true
                                sourceComponent: {
                                    switch (modelData.type) {
                                    case "dpi": return dpiComponent
                                    case "choice": return choiceComponent
                                    case "toggle": return toggleComponent
                                    case "slider": return sliderComponent
                                    case "color": return colorComponent
                                    case "action": return actionComponent
                                    }
                                    return null
                                }
                                Component {
                                    id: dpiComponent
                                    DpiEditor {
                                        control: loader.modelData
                                        device: content.device
                                    }
                                }
                                Component {
                                    id: choiceComponent
                                    ChoiceRow {
                                        control: loader.modelData
                                        onChosen: value => root.applyKey(content.device, loader.modelData.key, value)
                                    }
                                }
                                Component {
                                    id: toggleComponent
                                    HeldSwitch {
                                        text: root.tr(loader.modelData.label)
                                        checked: loader.modelData.value === true
                                        onToggled: on => root.applyKey(content.device, loader.modelData.key, on)
                                    }
                                }
                                Component {
                                    id: sliderComponent
                                    SliderRow {
                                        control: loader.modelData
                                        onMoved: value => root.applyKey(content.device, loader.modelData.key, value)
                                    }
                                }
                                Component {
                                    id: colorComponent
                                    ColorRow {
                                        control: loader.modelData
                                        onPicked: value => root.applyKey(content.device, loader.modelData.key, value)
                                    }
                                }
                                Component {
                                    id: actionComponent
                                    PlasmaComponents3.Button {
                                        text: root.tr(loader.modelData.label)
                                        onClicked: root.applyKey(content.device, loader.modelData.key, true)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
