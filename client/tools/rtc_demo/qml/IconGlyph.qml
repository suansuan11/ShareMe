import QtQuick

Item {
    id: glyph
    property string name: ""
    property real size: 20
    property color color: "#EDF2F7"
    property bool muted: false
    property real strokeWidth: 1.4
    implicitWidth: size
    implicitHeight: size

    function drawMicrophone(context) {
        context.beginPath()
        context.moveTo(12, 4)
        context.arc(12, 7, 3, -Math.PI / 2, Math.PI, true)
        context.lineTo(9, 12)
        context.arc(12, 12, 3, Math.PI, 0, false)
        context.lineTo(15, 7)
        context.arc(12, 7, 3, 0, -Math.PI / 2, true)
        context.stroke()
        context.beginPath()
        context.moveTo(6.5, 11.5)
        context.arc(12, 11.5, 5.5, 0, Math.PI, false)
        context.moveTo(12, 17)
        context.lineTo(12, 19.5)
        context.moveTo(9, 20)
        context.lineTo(15, 20)
        context.stroke()
    }

    function drawSpeaker(context) {
        context.beginPath()
        context.moveTo(4, 10)
        context.lineTo(8, 10)
        context.lineTo(13, 6.5)
        context.lineTo(13, 17.5)
        context.lineTo(8, 14)
        context.lineTo(4, 14)
        context.closePath()
        context.stroke()
        context.beginPath()
        context.arc(13, 12, 4, -Math.PI / 3, Math.PI / 3, false)
        context.moveTo(16.5, 7.5)
        context.arc(13, 12, 6.2, -Math.PI / 4, Math.PI / 4, false)
        context.stroke()
    }

    function drawDetails(context) {
        context.beginPath()
        context.arc(6, 12, 1.5, 0, Math.PI * 2, false)
        context.arc(12, 12, 1.5, 0, Math.PI * 2, false)
        context.arc(18, 12, 1.5, 0, Math.PI * 2, false)
        context.fill()
    }

    function drawLeave(context) {
        context.beginPath()
        context.moveTo(14, 5)
        context.lineTo(19, 5)
        context.lineTo(19, 19)
        context.lineTo(14, 19)
        context.moveTo(5, 12)
        context.lineTo(16, 12)
        context.moveTo(12, 8)
        context.lineTo(16, 12)
        context.lineTo(12, 16)
        context.stroke()
    }

    function drawCopy(context) {
        context.beginPath()
        context.moveTo(9, 8)
        context.lineTo(18, 8)
        context.lineTo(18, 19)
        context.lineTo(9, 19)
        context.closePath()
        context.stroke()
        context.beginPath()
        context.moveTo(6, 16)
        context.lineTo(6, 5)
        context.lineTo(15, 5)
        context.stroke()
    }

    function drawSettings(context) {
        context.beginPath()
        context.moveTo(5, 7)
        context.lineTo(19, 7)
        context.moveTo(5, 12)
        context.lineTo(19, 12)
        context.moveTo(5, 17)
        context.lineTo(19, 17)
        context.stroke()
        context.beginPath()
        context.arc(9, 7, 1.8, 0, Math.PI * 2, false)
        context.arc(15, 12, 1.8, 0, Math.PI * 2, false)
        context.arc(11, 17, 1.8, 0, Math.PI * 2, false)
        context.fill()
    }

    function drawHelp(context) {
        context.beginPath()
        context.arc(12, 12, 8, 0, Math.PI * 2, false)
        context.stroke()
        context.beginPath()
        context.arc(12, 9, 2.5, Math.PI, 0, false)
        context.lineTo(12, 13)
        context.moveTo(12, 17)
        context.arc(12, 17, 0.8, 0, Math.PI * 2, false)
        context.stroke()
    }

    function drawBack(context) {
        context.beginPath()
        context.moveTo(19, 12)
        context.lineTo(5, 12)
        context.moveTo(5, 12)
        context.lineTo(11, 6)
        context.moveTo(5, 12)
        context.lineTo(11, 18)
        context.stroke()
    }

    function drawClose(context) {
        context.beginPath()
        context.moveTo(6, 6)
        context.lineTo(18, 18)
        context.moveTo(18, 6)
        context.lineTo(6, 18)
        context.stroke()
    }

    function drawWarning(context) {
        context.beginPath()
        context.moveTo(12, 4)
        context.lineTo(20, 19)
        context.lineTo(4, 19)
        context.closePath()
        context.stroke()
        context.beginPath()
        context.moveTo(12, 9)
        context.lineTo(12, 14)
        context.moveTo(12, 17)
        context.arc(12, 17, 0.8, 0, Math.PI * 2, false)
        context.stroke()
    }

    function drawShare(context) {
        context.beginPath()
        context.moveTo(8, 12)
        context.lineTo(16, 7)
        context.moveTo(8, 12)
        context.lineTo(16, 17)
        context.stroke()
        context.beginPath()
        context.arc(6, 12, 2.5, 0, Math.PI * 2, false)
        context.arc(18, 6, 2.5, 0, Math.PI * 2, false)
        context.arc(18, 18, 2.5, 0, Math.PI * 2, false)
        context.fill()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var context = getContext("2d")
            context.clearRect(0, 0, width, height)
            if (width <= 0 || height <= 0)
                return
            context.save()
            context.scale(width / 24, height / 24)
            context.strokeStyle = glyph.color
            context.fillStyle = glyph.color
            context.lineWidth = glyph.strokeWidth
            context.lineCap = "round"
            context.lineJoin = "round"
            if (glyph.name === "microphone")
                glyph.drawMicrophone(context)
            else if (glyph.name === "speaker")
                glyph.drawSpeaker(context)
            else if (glyph.name === "details")
                glyph.drawDetails(context)
            else if (glyph.name === "leave")
                glyph.drawLeave(context)
            else if (glyph.name === "copy")
                glyph.drawCopy(context)
            else if (glyph.name === "settings")
                glyph.drawSettings(context)
            else if (glyph.name === "help")
                glyph.drawHelp(context)
            else if (glyph.name === "back")
                glyph.drawBack(context)
            else if (glyph.name === "close")
                glyph.drawClose(context)
            else if (glyph.name === "warning")
                glyph.drawWarning(context)
            else if (glyph.name === "share")
                glyph.drawShare(context)
            if (glyph.muted) {
                context.beginPath()
                context.moveTo(4, 4)
                context.lineTo(20, 20)
                context.stroke()
            }
            context.restore()
        }
        Component.onCompleted: requestPaint()
    }

    onNameChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()
    onSizeChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onMutedChanged: canvas.requestPaint()
    onStrokeWidthChanged: canvas.requestPaint()
}
