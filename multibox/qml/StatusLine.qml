import QtQuick
Row {
 property string label: "CS2"
 property string value: "Не запущена"
 property bool online: false
 property bool warning: false
 spacing: 9
 Rectangle { width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: parent.online ? Theme.shade("#50dcb5") : parent.warning ? Theme.shade("#e6bc70") : Theme.shade("#506179") }
 Text { text: parent.label; color: Theme.shade("#8499b1"); font.family: "Segoe UI"; font.pixelSize: 12; width: 58 }
 Text { text: parent.value; color: parent.online ? Theme.shade("#c3eede") : Theme.shade("#94a6ba"); font.family: "Segoe UI"; font.pixelSize: 12 }
}
