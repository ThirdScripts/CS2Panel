import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
 id: app
 Binding { target: Theme; property: "name"; value: panel.theme }
 width: Math.min(1320, Screen.width - 60); height: Math.min(910, Screen.height - 60)
 minimumWidth: 1000; minimumHeight: 680
 visible: true; color: Theme.shade("#070d16"); title: "CS2 Panel"
 flags: Qt.Window | Qt.FramelessWindowHint
 property bool intro: true
 property bool details: panel.selectedId > 0
 property bool navigatorPage: false
 property bool infoOpen: false
 property bool helpOpen: false
 property bool diagnosticsOpen: false
 property bool recoveryOpen: recoveryDialog.visible
 property string recoveryText: ""
 property string diagnosticsText: ""
 property string notification: ""
 property bool notificationError: false
 property var current: panel.selected
 property int gamesOnline: { let n=0; for(let item of panel.environments) if(item.game) n++; return n }
 property int botsOnline: { let n=0; for(let item of panel.environments) if(item.bot === "работает") n++; return n }
 font.family: Theme.fontFamily
 function hostUpdate() {
  let p = nativeRegion.mapToItem(app.contentItem, 0, 0)
  panel.navigatorRect(p.x,p.y,nativeRegion.width,nativeRegion.height,details && navigatorPage && !!current.hasBot && !infoOpen && !helpOpen && !diagnosticsOpen && !recoveryOpen && !intro && app.visibility !== Window.Minimized)
 }
 onWidthChanged: hostDelay.restart()
 onHeightChanged: hostDelay.restart()
 onNavigatorPageChanged: hostDelay.restart()
 onInfoOpenChanged: hostDelay.restart()
 onHelpOpenChanged: hostDelay.restart()
 onDiagnosticsOpenChanged: hostDelay.restart()
 onRecoveryOpenChanged: hostDelay.restart()
 onVisibilityChanged: { panel.setActive(app.visibility !== Window.Minimized); hostDelay.restart() }
 Timer { id: hostDelay; interval: 80; onTriggered: app.hostUpdate() }
 Timer { id: toastTimer; interval: 4800; onTriggered: app.notification = "" }
 Connections {
  target: panel
  function onNotice(message, error) { app.notification=message; app.notificationError=error; if(recoveryOpen) recoveryText=message; toastTimer.restart() }
  function onChanged() { hostDelay.restart() }
 }
 Shortcut { sequence: "Escape"; onActivated: { if(diagnosticsOpen) diagnosticsOpen=false; else if(infoOpen) infoOpen=false; else if(helpOpen) helpOpen=false; else if(details) { navigatorPage=false;panel.select(-1) } } }
 Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.shade("#25374b"); z: 99 }
 Dialog {
  id: recoveryDialog
  property int environmentId: -1
  parent: Overlay.overlay
  anchors.centerIn: parent
  width: 630; modal: true
  title: "Восстановление профиля навигатора"
  closePolicy: Popup.CloseOnEscape
  contentItem: ColumnLayout {
   spacing: 16
   Label {
    Layout.fillWidth: true; wrapMode: Text.WordWrap
    text: "1. Запусти CS2 в выбранном окружении.\n2. Открой локальный матч с ботами и дождись появления персонажа.\n3. Вернись сюда и нажми «Собрать данные».\n\nСбор только читает данные. Управление персонажем не выполняется."
   }
   TextArea {
    Layout.fillWidth: true; Layout.preferredHeight: 100
    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
    text: panel.busy ? "Собираю данные выбранного клиента…" : recoveryText
   }
   RowLayout {
    Button { text: "Собрать данные"; enabled: !panel.busy; onClicked: panel.checkCompatibility(recoveryDialog.environmentId) }
    Button { text: "Закрыть"; onClicked: recoveryDialog.close() }
   }
  }
 }

 // Custom chrome: only pointer-driven window operations, no background animation.
 Rectangle {
  id: titlebar; width: parent.width; height: 44; color: Theme.shade("#080f19"); z: 10
  MouseArea { anchors.fill: parent; onPressed: app.startSystemMove(); onDoubleClicked: app.visibility===Window.Maximized ? app.showNormal() : app.showMaximized() }
  Row { anchors.left: parent.left; anchors.leftMargin: 22; anchors.verticalCenter: parent.verticalCenter; spacing: 10
   Rectangle { width: 7; height: 7; radius: 2; color: Theme.shade("#3bc9f5"); anchors.verticalCenter: parent.verticalCenter }
   Text { font.family: Theme.fontFamily; text: Theme.terminal ? "~/cs2-panel — session manager" : Theme.kitty ? "CS2 PANEL  /  HELLO KITTY" : "CS2 PANEL"; color: Theme.shade("#96aec6"); font.pixelSize: 10; font.letterSpacing: 2; font.weight: Font.DemiBold }
  }
  Row { anchors.right: parent.right; height: parent.height
   Repeater { model: ["—", "□", "×"]
    Rectangle { required property int index; required property string modelData; width: 46; height: 44; color: chromeMouse.containsMouse ? (index===2 ? Theme.shade("#8d304a") : Theme.shade("#182739")) : "transparent"
     Text { font.family: Theme.fontFamily; anchors.centerIn: parent; text: modelData; color: Theme.shade("#adbed0"); font.pixelSize: 17 }
     MouseArea { id: chromeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: index===0 ? app.showMinimized() : index===1 ? (app.visibility===Window.Maximized ? app.showNormal() : app.showMaximized()) : app.close() }
    }
   }
  }
 }
 Rectangle {
  id: sidebar; y: titlebar.height; width: 210; height: parent.height-y; color: Theme.shade("#0a121e")
  Rectangle { width: 1; height: parent.height; anchors.right: parent.right; color: Theme.shade("#192839") }
  Column { x: 23; y: 30; spacing: 8
   Row { spacing: 12
    Rectangle { width: 35; height: 35; radius: 10; color: Theme.shade("#103149"); border.color: Theme.shade("#246181")
     Text { font.family: Theme.fontFamily; anchors.centerIn: parent; text: "C"; font.pixelSize: 22; font.weight: Font.Bold; color: Theme.shade("#58d5ff") }
    }
    Column { spacing: 1; anchors.verticalCenter: parent.verticalCenter
     Text { font.family: Theme.fontFamily; text: "CS2 Panel"; color: Theme.shade("#ecf4ff"); font.pixelSize: 18; font.weight: Font.DemiBold }
     Text { font.family: Theme.fontFamily; text: Theme.terminal ? "user@cs2:~$" : Theme.kitty ? "HELLO, FRIEND!" : "WORKSPACE"; color: Theme.shade("#526c88"); font.pixelSize: 9; font.letterSpacing: 2 }
    }
   }
  }
  Text { font.family: Theme.fontFamily; x: 24; y: 124; text: "УПРАВЛЕНИЕ"; color: Theme.shade("#516882"); font.pixelSize: 9; font.letterSpacing: 1.6 }
  Rectangle { x: 12; y: 152; width: 186; height: 46; radius: 9; color: Theme.shade("#11283b"); border.color: Theme.shade("#1c425b")
   Rectangle { x: 0; y: 12; width: 3; height: 22; radius: 2; color: Theme.shade("#3fceff") }
   Text { font.family: Theme.fontFamily; x: 17; anchors.verticalCenter: parent.verticalCenter; text: "▦   Окружения"; color: Theme.shade("#7cddff"); font.pixelSize: 13; font.weight: Font.DemiBold }
   MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { navigatorPage=false;panel.select(-1) } }
  }
  Rectangle { x: 12; y: 207; width: 186; height: 44; radius: 9; color: helpHover.containsMouse ? Theme.shade("#111f30") : "transparent"
   Text { font.family: Theme.fontFamily; x: 17; anchors.verticalCenter: parent.verticalCenter; text: "?    Как пользоваться"; color: Theme.shade("#7e94ae"); font.pixelSize: 12 }
   MouseArea { id: helpHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: helpOpen=true }
  }

  Column { x: 24; y: 270; spacing: 8
   Text { font.family: Theme.fontFamily; text: "ТЕМА"; color: Theme.shade("#8499b1"); font.pixelSize: 10 }
   Repeater { model: [{key:"blue",label:"Синяя"},{key:"black",label:"Чёрная"},{key:"kitty",label:"Hello Kitty"},{key:"terminal",label:"Linux terminal"}]
    ActionButton { required property var modelData; width: 162; implicitHeight: 34; text: (panel.theme===modelData.key ? "●  " : "○  ")+modelData.label; onClicked: panel.setTheme(modelData.key); Accessible.name: modelData.label }
   }
  }
  Rectangle { visible: sidebar.height > 710; x: 16; width: 178; height: 126; anchors.bottom: profile.top; anchors.bottomMargin: 22; radius: 12; color: Theme.shade("#0e1c2b"); border.color: Theme.shade("#1b3043")
   Column { x: 15; y: 16; spacing: 10
    Text { font.family: Theme.fontFamily; text: "ОБЩАЯ УСТАНОВКА"; color: Theme.shade("#56768d"); font.pixelSize: 8; font.letterSpacing: 1.4 }
    Text { font.family: Theme.fontFamily; text: "Свои аккаунты.\nОдна библиотека."; color: Theme.shade("#a7bdd0"); font.pixelSize: 13; lineHeight: 1.25 }
    Text { font.family: Theme.fontFamily; text: "Sandboxie  ·  Windows 10"; color: Theme.shade("#4d7891"); font.pixelSize: 9 }
   }
  }
  Rectangle { visible: Theme.kitty; x: 23; y: 465; width: 164; height: Math.min(168,sidebar.height-550); color: "#ffffff"; radius: 12; border.color: "#e9adc6"; clip: true
  }
  Item { id: profile; x: 20; width: 170; height: 70; anchors.bottom: parent.bottom
   Rectangle { y: 4; width: 34; height: 34; radius: 17; color: Theme.shade("#19374d"); Text { font.family: Theme.fontFamily; anchors.centerIn: parent; text: "b"; color: Theme.shade("#69dbff"); font.pixelSize: 18; font.weight: Font.DemiBold } }
   Column { x: 46; y: 5; spacing: 3; Text { font.family: Theme.fontFamily; text: "CS2Panel"; color: Theme.shade("#d4e5f5"); font.pixelSize: 13 } Text { font.family: Theme.fontFamily; text: "Локальное управление"; color: Theme.shade("#58718a"); font.pixelSize: 9 } }
  }
 }

 Item {
  id: workspace; x: sidebar.width; y: titlebar.height; width: parent.width-x; height: parent.height-y
  Rectangle { anchors.fill: parent; gradient: Gradient { GradientStop { position: 0; color: Theme.shade("#0b1725") } GradientStop { position: .55; color: Theme.shade("#080f19") } GradientStop { position: 1; color: Theme.shade("#070d16") } } }
  // Quiet architectural lines instead of costly blur layers.
  Repeater { model: 4; Rectangle { required property int index; x: workspace.width-280+index*65; y: 0; width: 1; height: 160-index*23; rotation: 35; color: Theme.shade("#14283b"); opacity: .55 } }
  Item {
   id: overview; anchors.fill: parent; anchors.margins: 32; visible: !details
   opacity: visible ? 1 : 0
   Behavior on opacity { NumberAnimation { duration: 180 } }
   Text { font.family: Theme.fontFamily; id: eyebrow; text: Theme.kitty ? "HELLO KITTY  /  SWEET WORKSPACE" : "ВАШ КОМАНДНЫЙ ЦЕНТР"; color: Theme.shade("#4ca6c9"); font.pixelSize: 9; font.letterSpacing: 2.1 }
   Text { font.family: Theme.fontFamily; id: heading; y: 24; text: "Окружения"; color: Theme.shade("#edf6ff"); font.pixelSize: 34; font.weight: Font.DemiBold; font.letterSpacing: -.7 }
   Text { font.family: Theme.fontFamily; y: 73; text: "Каждый аккаунт — в своём пространстве."; color: Theme.shade("#778ea7"); font.pixelSize: 13 }
   ActionButton { anchors.right: parent.right; y: 30; primary: true; text: "Добавить окружение"; symbol: "+"; enabled: !panel.busy; onClicked: panel.addEnvironment() }
   ActionButton { anchors.right: parent.right; y: 105; implicitHeight: 32; text: "Проверить профили"; font.pixelSize: 11; enabled: !panel.busy; onClicked: panel.checkCompatibility() }
   Row { y: 119; spacing: 18
    Text { font.family: Theme.fontFamily; text: String(panel.environments.length).padStart(2,"0") + "  ОКРУЖЕНИЙ"; color: Theme.shade("#7189a3"); font.pixelSize: 10; font.letterSpacing: .8 }
    Text { font.family: Theme.fontFamily; text: "●  " + gamesOnline + " В ИГРЕ"; color: gamesOnline ? Theme.shade("#56c9b0") : Theme.shade("#587088"); font.pixelSize: 10; font.letterSpacing: .8 }
    Text { font.family: Theme.fontFamily; text: "●  " + botsOnline + " БОТОВ АКТИВНО"; color: botsOnline ? Theme.shade("#68cbee") : Theme.shade("#587088"); font.pixelSize: 10; font.letterSpacing: .8 }
   }
   Rectangle { y: 151; width: parent.width; height: 1; color: Theme.shade("#1b2a3b") }
   ScrollView {
    id: cardScroll; y: 175; width: parent.width; height: parent.height-y-35; clip: true
    contentWidth: availableWidth
    GridLayout { width: cardScroll.availableWidth; columns: width > 900 ? 3 : 2; columnSpacing: 16; rowSpacing: 16
     Repeater { model: panel.environments.length
      Rectangle {
       id: card; required property int index; property var modelData: panel.environments[index]
       Layout.fillWidth: true; Layout.preferredHeight: 207; radius: Theme.terminal ? 2 : 15
       SequentialAnimation on opacity { PauseAnimation { duration: card.index*35 } NumberAnimation { from: 0; to: 1; duration: 230; easing.type: Easing.OutCubic } }
       color: cardHover.containsMouse ? Theme.shade("#132438") : Theme.shade("#0e1b2b")
       border.color: cardHover.containsMouse ? Theme.shade("#317b9d") : Theme.shade("#203349")
       Behavior on color { ColorAnimation { duration: 160 } }
       Behavior on border.color { ColorAnimation { duration: 180 } }
       Rectangle { visible: Theme.kitty; x: 12; y: 10; width: parent.width-24; height: parent.height-20; radius: 12; color: "#fff9fc"; border.color: "#f1c4d7" }
       Rectangle { visible: Theme.kitty; x: parent.width/2-19; y: -4; width: 38; height: 12; rotation: -5; radius: 2; color: "#eaa0bf"; opacity: .7 }
       Rectangle { visible: Theme.kitty; anchors.right: parent.right; anchors.rightMargin: 15; y: 158; width: 42; height: 40; radius: 5; color: "white"; rotation: card.index%2 ? 6 : -6; border.color: "#efc1d3"
       }
       Rectangle { x: 18; y: 0; width: 45; height: 2; radius: 1; color: card.modelData.game ? Theme.shade("#3ecdf5") : Theme.shade("#294960") }
       Text { font.family: Theme.fontFamily; x: 21; y: 19; text: "СРЕДА / " + String(card.modelData.id).padStart(2,"0"); color: Theme.shade("#55728f"); font.pixelSize: 9; font.letterSpacing: 1.4 }
       Text { font.family: Theme.fontFamily; anchors.right: parent.right; anchors.rightMargin: 20; y: 13; text: "↗"; color: cardHover.containsMouse ? Theme.shade("#68dcff") : Theme.shade("#3c5874"); font.pixelSize: 21 }
       Text { font.family: Theme.fontFamily; x: 21; y: 49; width: parent.width-42; text: card.modelData.name; elide: Text.ElideRight; color: Theme.shade("#e1ecf9"); font.pixelSize: 20; font.weight: Font.DemiBold }
       Rectangle { x: 21; y: 92; width: parent.width-42; height: 1; color: Theme.shade("#213246") }
       StatusLine { x: 21; y: 112; label: "CS2"; value: card.modelData.game ? "В игре" : "Не запущена"; online: card.modelData.game }
       StatusLine { x: 21; y: 141; label: "Бот"; value: card.modelData.hasBot ? card.modelData.bot : "Не подключён"; online: card.modelData.bot === "работает"; warning: card.modelData.hasBot && !online }
       Text { font.family: Theme.fontFamily; x: 21; y: 176; text: "Открыть управление"; color: cardHover.containsMouse ? Theme.shade("#70cee9") : Theme.shade("#4b657f"); font.pixelSize: 10 }
       MouseArea { id: cardHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { navigatorPage=false;panel.select(card.modelData.id) } }
       activeFocusOnTab: true
       Keys.onReturnPressed: { navigatorPage=false;panel.select(card.modelData.id) }
       Rectangle { anchors.fill: parent; color: "transparent"; radius: Theme.terminal ? 2 : 15; border.color: Theme.shade("#62d5ff"); visible: card.activeFocus }
      }
     }
    }
   }
   Text { font.family: Theme.fontFamily; anchors.bottom: parent.bottom; text: "Выберите карточку, чтобы открыть Steam, CS2 и навигатор."; color: Theme.shade("#526b85"); font.pixelSize: 11 }
  }

  Item {
   id: detail; anchors.fill: parent; anchors.margins: 28; visible: details; opacity: visible ? 1 : 0
   Behavior on opacity { NumberAnimation { duration: 220 } }
   Text { font.family: Theme.fontFamily; text: "‹   Окружения"; color: backMouse.containsMouse ? Theme.shade("#70d7ff") : Theme.shade("#7197b3"); font.pixelSize: 12; MouseArea { id: backMouse; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { navigatorPage=false;panel.select(-1) } } }
   RowLayout { y: 33; width: parent.width; spacing: 12
    TextField { id: nameField; Layout.fillWidth: true; text: current.name || ""; maximumLength: 64; color: Theme.shade("#ebf6ff"); font.pixelSize: 28; font.weight: Font.DemiBold; selectByMouse: true; padding: 8; background: Rectangle { radius: 8; color: nameField.activeFocus ? Theme.shade("#14273a") : "transparent"; border.color: nameField.activeFocus ? Theme.shade("#3381a4") : "transparent" } onEditingFinished: if(text !== current.name && text.trim()) panel.rename(panel.selectedId,text) }
    ActionButton { text: "Информация"; symbol: "ⓘ"; onClicked: infoOpen=true }
   }
   Row { y: 95; spacing: 24
    StatusLine { label: "CS2"; value: current.game ? "Запущена" : "Не запущена"; online: current.game || false }
    StatusLine { label: "Бот"; value: current.hasBot ? current.bot : "Не подключён"; online: current.bot === "работает" }
   }
   Row { y: 136; spacing: 24
    Repeater { model: ["Управление", "Навигатор"]
     Item { required property string modelData; required property int index; width: 116; height: 36
      Text { font.family: Theme.fontFamily; text: modelData; color: navigatorPage === (index===1) ? Theme.shade("#68d8ff") : Theme.shade("#637c97"); font.pixelSize: 13 }
      Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; radius: 1; color: Theme.shade("#42cafd"); visible: navigatorPage === (index===1) }
      MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: navigatorPage=index===1 }
     }
    }
   }
   Rectangle { y: 173; width: parent.width; height: 1; color: Theme.shade("#1b2e42") }
   Column { y: 200; width: parent.width; spacing: 20; visible: !navigatorPage
    Rectangle { width: parent.width; height: 158; radius: 14; color: Theme.shade("#101f30"); border.color: Theme.shade("#20384d")
     Column { x: 23; y: 20; spacing: 8
      Text { font.family: Theme.fontFamily; text: "Игровая сессия"; color: Theme.shade("#dcecfb"); font.pixelSize: 18; font.weight: Font.DemiBold }
      Text { font.family: Theme.fontFamily; text: "Войдите в свой аккаунт Steam и запустите клиент."; color: Theme.shade("#758ea7"); font.pixelSize: 12 }
      Row { spacing: 12; topPadding: 12
       ActionButton { text: "Открыть Steam"; enabled: !panel.busy; onClicked: panel.action(panel.selectedId,"steam") }
       ActionButton { text: "Запустить CS2"; primary: true; enabled: !panel.busy && !current.game; onClicked: panel.action(panel.selectedId,"game") }
       ActionButton { text: "Показать CS2"; enabled: !panel.busy && !!current.game; onClicked: panel.action(panel.selectedId,"switch") }
       ActionButton { text: "Диагностика"; enabled: !panel.busy; onClicked: {diagnosticsText=panel.diagnoseEnvironment(panel.selectedId);diagnosticsOpen=true} }
      }
     }
    }
    Rectangle { width: parent.width; height: 158; radius: 14; color: Theme.shade("#0e1b2a"); border.color: Theme.shade("#203247")
     Column { x: 23; y: 20; spacing: 8
      Text { font.family: Theme.fontFamily; text: "Навигатор"; color: Theme.shade("#dcecfb"); font.pixelSize: 18; font.weight: Font.DemiBold }
      Text { font.family: Theme.fontFamily; text: "Управление только клиентом выбранного окружения."; color: Theme.shade("#758ea7"); font.pixelSize: 12 }
      Row { spacing: 12; topPadding: 12
       ActionButton { text: current.hasBot ? "Открыть навигатор" : "Подключить навигатор"; primary: true; enabled: !panel.busy && !!current.game; onClicked: {panel.action(panel.selectedId,"bot"); navigatorPage=true} }
       ActionButton { text: "Остановить бота"; danger: true; enabled: !panel.busy && !!current.hasBot; onClicked: panel.action(panel.selectedId,"stop") }
       ActionButton { text: "Восстановить профиль"; enabled: !panel.busy; onClicked: {recoveryDialog.environmentId=panel.selectedId;recoveryText="";recoveryDialog.open()} }
      }
     }
    }
    Text { font.family: Theme.fontFamily; text: "F10 — аварийная остановка навигаторов.\nИмя окружения можно изменить прямо в заголовке."; color: Theme.shade("#58738e"); font.pixelSize: 11; lineHeight: 1.6 }
   }
   Item { id: nativeRegion; y: 194; width: parent.width; height: parent.height-y; visible: navigatorPage
    Rectangle { anchors.fill: parent; radius: 13; color: Theme.shade("#0d1b2a"); border.color: Theme.shade("#284258") }
    Column { anchors.centerIn: parent; spacing: 16; visible: !current.hasBot
     Text { font.family: Theme.fontFamily; anchors.horizontalCenter: parent.horizontalCenter; text: "Навигатор ещё не подключён"; color: Theme.shade("#a6bfd6"); font.pixelSize: 17 }
     ActionButton { anchors.horizontalCenter: parent.horizontalCenter; text: "Подключить"; primary: true; enabled: !!current.game && !panel.busy; onClicked: panel.action(panel.selectedId,"bot") }
    }
   }
  }
 }
 Rectangle {
  id: shade; anchors.fill: parent; color: Theme.shade("#b3050b12"); visible: infoOpen || helpOpen || diagnosticsOpen; z: 20
  MouseArea { anchors.fill: parent; onClicked: { infoOpen=false;helpOpen=false } }
 }
 Rectangle {
  id: info; z: 21; width: 370; height: parent.height-titlebar.height; y: titlebar.height; x: infoOpen ? parent.width-width : parent.width; color: Theme.shade("#0d1b2a"); border.color: Theme.shade("#27445b")
  Behavior on x { NumberAnimation { duration: 230; easing.type: Easing.OutCubic } }
  Column { anchors.fill: parent; anchors.margins: 28; spacing: 24
   Row { spacing: 20; Text { font.family: Theme.fontFamily; text: "Информация"; color: Theme.shade("#e0f2ff"); font.pixelSize: 23; width: 245 } Text { font.family: Theme.fontFamily; text: "×"; color: Theme.shade("#8faabe"); font.pixelSize: 24; MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: infoOpen=false } } }
   Text { font.family: Theme.fontFamily; text: current.name || ""; color: Theme.shade("#5eceef"); font.pixelSize: 15; width: parent.width; elide: Text.ElideRight }
   Repeater { model: [ ["ПРОЦЕСС CS2 / STEAM",current.pid || "Не запущен"], ["CPU · ДОЛЯ ВСЕГО ПК",Number(current.cpu || 0).toFixed(1)+" %"], ["RAM · WORKING SET",Math.round(current.ram || 0)+" МБ"], ["ОКРУЖЕНИЕ",current.box || "—"] ]
    Column { required property var modelData; width: 310; spacing: 8
     Text { font.family: Theme.fontFamily; text: modelData[0]; color: Theme.shade("#5e7d98"); font.pixelSize: 9; font.letterSpacing: 1 }
     Text { font.family: Theme.fontFamily; text: String(modelData[1]); color: Theme.shade("#d2e5f7"); font.pixelSize: 22; width: parent.width; elide: Text.ElideRight }
     Rectangle { width: parent.width; height: 1; color: Theme.shade("#1c3449") }
    }
   }
   Text { font.family: Theme.fontFamily; width: parent.width; text: "CPU и RAM — выбранный процесс. Обновление раз в 2 секунды."; wrapMode: Text.WordWrap; color: Theme.shade("#6d879f"); font.pixelSize: 11; lineHeight: 1.4 }
  }
 }
 Rectangle { z: 23; visible: diagnosticsOpen; anchors.centerIn: parent; width: Math.min(720,app.width-60); height: Math.min(580,app.height-80); color: Theme.shade("#101e2e"); border.color: Theme.shade("#2a4961"); radius: 12
  ColumnLayout { anchors.fill: parent; anchors.margins: 22; spacing: 15
   Text { text: "Диагностика окружения"; font.family: Theme.fontFamily; font.pixelSize: 22; color: Theme.shade("#e4f4ff") }
   ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true
    TextArea { text: diagnosticsText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: Theme.fontFamily; font.pixelSize: 13; color: Theme.shade("#dce9f6"); background: null }
   }
   ActionButton { text: "Закрыть"; onClicked: diagnosticsOpen=false }
  }
 }
 Rectangle { z: 22; visible: helpOpen; anchors.centerIn: parent; width: 560; height: 370; radius: 18; color: Theme.shade("#101e2e"); border.color: Theme.shade("#2a4961")
  Column { anchors.fill: parent; anchors.margins: 30; spacing: 20
   Text { font.family: Theme.fontFamily; text: "Всё начинается с карточки"; color: Theme.shade("#e4f4ff"); font.pixelSize: 23 }
   Text { font.family: Theme.fontFamily; width: parent.width; text: "01   Выберите окружение и откройте Steam.\n02   Используйте отдельный аккаунт для каждой среды.\n03   Запустите CS2, затем подключите навигатор.\n04   Его настройки доступны во вкладке «Навигатор»."; color: Theme.shade("#97b0c7"); font.pixelSize: 13; lineHeight: 2; wrapMode: Text.WordWrap }
   Text { font.family: Theme.fontFamily; width: parent.width; text: "Основная библиотека игры общая. Проверку файлов и обновления выполняйте только из одного Steam одновременно."; color: Theme.shade("#65859d"); font.pixelSize: 12; wrapMode: Text.WordWrap }
   ActionButton { text: "Понятно"; primary: true; onClicked: helpOpen=false }
  }
 }
 Rectangle { z: 30; anchors.bottom: parent.bottom; anchors.bottomMargin: 22; anchors.horizontalCenter: parent.horizontalCenter; width: Math.min(680,parent.width-100); height: toastText.implicitHeight+28; radius: 11; color: notificationError ? Theme.shade("#341e2c") : Theme.shade("#102f3d"); border.color: notificationError ? Theme.shade("#87425b") : Theme.shade("#286981"); opacity: notification ? 1 : 0; visible: opacity>0
  Behavior on opacity { NumberAnimation {duration: 180} }
  Text { font.family: Theme.fontFamily; id: toastText; x: 18; y: 14; width: parent.width-36; text: notification; color: notificationError ? Theme.shade("#ffbacb") : Theme.shade("#bcecff"); font.pixelSize: 12; wrapMode: Text.WordWrap }
 }
 Rectangle { z: 40; x: sidebar.width; y: 43; height: 2; width: parent.width-sidebar.width; visible: panel.busy; color: Theme.shade("#16354a")
  Rectangle { width: parent.width*.25; height: 2; color: Theme.shade("#64dbff"); SequentialAnimation on x { running: panel.busy && app.visibility!==Window.Minimized; loops: Animation.Infinite; NumberAnimation { from: 0; to: workspace.width*.75; duration: 900; easing.type: Easing.InOutSine } NumberAnimation { to: 0; duration: 900; easing.type: Easing.InOutSine } } }
 }
 Rectangle {
  id: welcome; anchors.fill: parent; color: Theme.shade("#080f19"); z: 50; visible: opacity>0; opacity: intro ? 1 : 0
  Behavior on opacity { NumberAnimation { duration: 350 } }
  Column { anchors.centerIn: parent; spacing: 21
   Text { font.family: Theme.fontFamily; anchors.horizontalCenter: parent.horizontalCenter; text: "CS2 / PANEL"; color: Theme.shade("#49c9f3"); font.pixelSize: 10; font.letterSpacing: 5 }
   Text { font.family: Theme.fontFamily; anchors.horizontalCenter: parent.horizontalCenter; text: "Добро пожаловать"; color: Theme.shade("#e7f4ff"); font.pixelSize: 31; font.weight: Font.DemiBold }
   Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 160; height: 2; color: Theme.shade("#1b3448"); Rectangle { height: 2; color: Theme.shade("#42d0ff"); NumberAnimation on width { from: 0; to: 160; duration: 1100; easing.type: Easing.InOutCubic } } }
   Text { font.family: Theme.fontFamily; anchors.horizontalCenter: parent.horizontalCenter; text: "Ваше пространство готово к работе"; color: Theme.shade("#587b96"); font.pixelSize: 12 }
  }
  Timer { interval: 1400; running: true; onTriggered: {intro=false;hostDelay.restart()} }
 }
}



