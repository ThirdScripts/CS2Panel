#pragma once
#include <QObject>
#include <QVariantList>
#include <QTimer>
#include <QQuickWindow>

class PanelBridge : public QObject {
 Q_OBJECT
 Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
 Q_PROPERTY(QVariantList environments READ environments NOTIFY changed)
 Q_PROPERTY(int selectedId READ selectedId NOTIFY changed)
 Q_PROPERTY(QVariantMap selected READ selected NOTIFY changed)
 Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
 explicit PanelBridge(QObject* parent=nullptr,const QString& testRoot={});
 ~PanelBridge();
 QVariantList environments()const{return data_;}
 int selectedId()const{return selected_;}
 QVariantMap selected()const;
 QString theme()const{return theme_;}
 Q_INVOKABLE void checkCompatibility(int environmentId=-1);
 Q_INVOKABLE void setTheme(const QString& value);
 bool busy()const{return busy_;}
 void setWindow(QQuickWindow* w){window_=w;}
 Q_INVOKABLE void select(int id);
 Q_INVOKABLE void rename(int id,const QString& text);
 Q_INVOKABLE void addEnvironment();
 Q_INVOKABLE void action(int id,const QString& operation);
 Q_INVOKABLE void navigatorRect(qreal x,qreal y,qreal width,qreal height,bool visible);
 Q_INVOKABLE void setActive(bool active);
 Q_INVOKABLE void poll();
 Q_INVOKABLE QString diagnostics();
 Q_INVOKABLE QString diagnoseEnvironment(int id);
Q_SIGNALS:
 void changed();
 void busyChanged();
 void themeChanged();
 void notice(QString message,bool error);
private:
 QString theme_=QStringLiteral("blue");
 QVariantList data_;
 int selected_=-1;
 bool busy_=false;
 bool testMode_=false;
 QTimer timer_;
 QQuickWindow* window_=nullptr;
 void* host_=nullptr;
};
