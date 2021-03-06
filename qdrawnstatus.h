#ifndef QDRAWNSTATUS_H
#define QDRAWNSTATUS_H

#include <QOpenGLWidget>

class QStatusTime : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit QStatusTime(QWidget *parent = 0);
    virtual QSize minimumSizeHint() const;

    void setTime(double time);

protected:
    void paintGL();

private:
    double currentTime;
    QString drawnText;
};

#endif // QDRAWNSTATUS_H
