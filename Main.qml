// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Window
import QtCore
import Local.Selector 1.0

Window {
    id: root
    title: "Selector de wallpapers"
    width: 1800
    height: 460
    visible: false
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint
    property bool appliedOnce: false
    property real wheelRemainder: 0
    property bool wheelUsesPixels: false
    // Read the model directly: currentItem can lag behind currentIndex while
    // PathView animates, especially when Enter follows a wheel/key event.
    readonly property var selection: gallery.currentIndex >= 0 && gallery.currentIndex < gallery.count
                                    ? selector.wallpapers[gallery.currentIndex] : null

    // Keep the previously chosen behavior, without displaying settings.
    Settings {
        id: preferences
        category: "appearance"
        property int transition: 1
        property bool automaticAccent: true
        property bool automaticAppColors: false
    }

    function confirm(id) {
        const chosen = id || (selection ? selection.id : "")
        if (chosen && selector.ready && !selector.busy && !appliedOnce)
            selector.apply(chosen, preferences.transition, preferences.automaticAccent,
                           preferences.automaticAppColors)
    }
    function step(delta) {
        if (gallery.count > 0)
            gallery.currentIndex = ((gallery.currentIndex + delta) % gallery.count + gallery.count) % gallery.count
    }
    function scroll(event) {
        // Own wheel input across the whole window, including the gaps between
        // cards. No mouse buttons are grabbed, so clicks and dragging still work.
        event.accepted = true
        if (selector.busy || appliedOnce || gallery.count === 0) {
            wheelRemainder = 0
            return
        }
        const pixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0
        const delta = pixels ? event.pixelDelta : event.angleDelta
        const amount = Math.abs(delta.y) >= Math.abs(delta.x) ? delta.y : delta.x
        if (amount === 0)
            return
        if (pixels !== wheelUsesPixels || wheelRemainder * amount < 0)
            wheelRemainder = 0
        wheelUsesPixels = pixels
        wheelRemainder += amount
        const threshold = pixels ? 60 : 120
        const steps = Math.trunc(wheelRemainder / threshold)
        if (steps !== 0) {
            step(-steps)
            wheelRemainder -= steps * threshold
        }
        wheelReset.restart()
    }
    Timer { id: wheelReset; interval: 200; onTriggered: root.wheelRemainder = 0 }
    function restoreSelection() {
        if (gallery.count === 0)
            return
        // The backend resolves the last successfully applied ID, with the
        // daemon's current wallpaper as fallback. Browsing/cancelling saves nothing.
        const chosen = selector.initialWallpaperId
        for (let i = 0; i < gallery.count; ++i) {
            if (selector.wallpapers[i].id === chosen) {
                gallery.currentIndex = i
                return
            }
        }
        // First run, removed wallpaper, or filtered out: show the recent cards.
        gallery.currentIndex = Math.max(0, Math.min(gallery.count - 1,
                                                   Math.floor(gallery.pathItemCount / 2)))
    }
    Component.onCompleted: restoreSelection()

    PathView {
        id: gallery
        objectName: "gallery"
        anchors.fill: parent
        anchors.margins: 5
        clip: true
        model: selector.wallpapers
        pathItemCount: Math.min(count, root.width > 1400 ? 7 : 5)
        cacheItemCount: 2
        preferredHighlightBegin: 0.5
        preferredHighlightEnd: 0.5
        highlightRangeMode: PathView.StrictlyEnforceRange
        snapMode: PathView.SnapToItem
        highlightMoveDuration: 230
        maximumFlickVelocity: 1200
        dragMargin: 60
        interactive: !selector.busy
        readonly property real cardWidth: Math.min(360, width / (root.width > 1400 ? 5.2 : 3.6))

        path: Path {
            startX: -gallery.cardWidth * 0.15
            startY: gallery.height / 2
            PathAttribute { name: "cardScale"; value: 0.88 }
            PathLine { x: gallery.width / 2; y: gallery.height / 2 }
            PathAttribute { name: "cardScale"; value: 1.0 }
            PathLine { x: gallery.width + gallery.cardWidth * 0.15; y: gallery.height / 2 }
            PathAttribute { name: "cardScale"; value: 0.88 }
        }

        delegate: Item {
            id: card
            required property int index
            required property var modelData
            width: gallery.cardWidth
            height: gallery.height - 8
            scale: PathView.cardScale ?? 0.9
            z: PathView.isCurrentItem ? 10 : 1

            SlantedPreview {
                id: preview
                anchors.fill: parent
                source: card.modelData.preview
                selected: card.PathView.isCurrentItem
            }
            MouseArea {
                anchors.fill: parent
                containmentMask: preview
                enabled: !selector.busy && !root.appliedOnce
                cursorShape: selector.busy ? Qt.BusyCursor : Qt.PointingHandCursor
                onClicked: {
                    gallery.currentIndex = card.index
                    root.confirm(card.modelData.id)
                }
            }
            Accessible.name: card.modelData.title
            Accessible.role: Accessible.Button
            Accessible.onPressAction: root.confirm(card.modelData.id)
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 100
        acceptedButtons: Qt.NoButton
        scrollGestureEnabled: true
        onWheel: function(event) { root.scroll(event) }
    }

    // Only failures get a message; the normal interface is images only.
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(640, root.width - 60)
        height: message.implicitHeight + 36
        radius: 14
        color: "#ef1e1e2e"
        border.color: "#c061ff"
        visible: selector.error.length > 0 || (selector.ready && gallery.count === 0)
        Text {
            id: message
            anchors.centerIn: parent
            width: parent.width - 36
            text: selector.error.length > 0 ? selector.error : "No hay fondos disponibles."
            color: "#f5e0dc"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: selector
        function onWallpapersChanged() { root.restoreSelection() }
        function onApplied() {
            root.appliedOnce = true
            if (selector.error.length === 0)
                closeAfterApply.start()
        }
        function onAccentWarning(message) { closeAfterApply.stop(); console.warn(message) }
        function onAppearanceWarning(message) { closeAfterApply.stop(); console.warn(message) }
    }
    Timer { id: closeAfterApply; interval: 600; onTriggered: root.close() }
    Shortcut { sequence: "Escape"; enabled: !selector.busy; onActivated: root.close() }
    Shortcut { sequence: "Left"; enabled: !selector.busy; onActivated: root.step(-1) }
    Shortcut { sequence: "Right"; enabled: !selector.busy; onActivated: root.step(1) }
    Shortcut { sequence: "Return"; onActivated: root.confirm() }
    Shortcut { sequence: "Enter"; onActivated: root.confirm() }
}
