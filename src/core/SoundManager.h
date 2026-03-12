#pragma once

#include <QObject>
#include <QSoundEffect>

class SoundManager : public QObject
{
    Q_OBJECT
public:
    static SoundManager& instance();

    void playDoneSound();
    void setVolume(int value);

private:
    explicit SoundManager(QObject *parent = nullptr);
    Q_DISABLE_COPY(SoundManager)

    QSoundEffect m_effect;
};
