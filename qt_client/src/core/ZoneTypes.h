#ifndef ZONETYPES_H
#define ZONETYPES_H

#include <QString>

enum class ZoneState { Safe, Warning, Danger };

struct Zone {
    QString name;
    ZoneState state = ZoneState::Safe;
    double temp = 0;
    double humidity = 0;
};

QString colorForState(ZoneState state);
QString textForState(ZoneState state);

#endif // ZONETYPES_H
