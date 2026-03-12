#include "SoundManager.h"
#include "core/ConfigManager.h"

SoundManager& SoundManager::instance()
{
    static SoundManager inst;
    return inst;
}

SoundManager::SoundManager(QObject *parent)
    : QObject(parent)
{
    const QString path =
        ConfigManager::instance()
            .get("ui.sound_path", "sounds/done.wav")
            .toString();

    m_effect.setSource(QUrl(QStringLiteral("qrc:/") + path));
    setVolume(
        ConfigManager::instance()
            .get("ui.sound_volume", 70)
            .toInt()
        );
}

void SoundManager::playDoneSound()
{
    m_effect.stop();
    m_effect.play();
}

void SoundManager::setVolume(int value)
{
    m_effect.setVolume(static_cast<qreal>(value) / 100.0);
}
