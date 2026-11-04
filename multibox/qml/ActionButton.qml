import QtQuick
import QtQuick.Controls
Button {
 id: control
 property bool primary: false
 property bool danger: false
 property string symbol: ""
 implicitHeight: 44
 implicitWidth: Math.max(130, label.implicitWidth + 36)
 hoverEnabled: true
 opacity: enabled ? 1 : 0.38
 font.family: Theme.fontFamily
 font.pixelSize: 13
 background: Rectangle {
  radius: Theme.terminal ? 2 : 10
  color: control.down ? Theme.shade("#173b50") : control.primary ? (control.hovered ? Theme.shade("#65d8ff") : Theme.shade("#39c7f5")) : control.hovered ? Theme.shade("#142638") : Theme.shade("#0e1927")
  border.color: control.primary ? Theme.shade("#6ddfff") : control.danger ? Theme.shade("#563142") : control.hovered ? Theme.shade("#35627d") : Theme.shade("#223348")
  Behavior on color { ColorAnimation { duration: 130 } }
  Behavior on border.color { ColorAnimation { duration: 150 } }
 }
 contentItem: Text { id: label; text: (control.symbol ? control.symbol + "  " : "") + control.text; color: control.primary ? Theme.shade("#041722") : control.danger ? Theme.shade("#ff9ab4") : Theme.shade("#dce9f6"); font.family: control.font.family; font.pixelSize: control.font.pixelSize; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
 Rectangle { anchors.fill: parent; anchors.margins: -3; radius: 13; color: "transparent"; border.color: Theme.shade("#56cfff"); visible: control.activeFocus }
 ToolTip.visible: hovered && !enabled
}

