#ifndef TRANSLATIONENTRY_H
#define TRANSLATIONENTRY_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QPoint>
#include <QSize>

struct TranslationEntry {
    QString id; // UUID or Timestamp
    QDateTime timestamp;
    QString originalBase64; // Used for runtime display
    QStringList originalBase64List; // Multi-image payload for batched translations
    QString localImagePath; // Used for storage path reference
    QStringList localImagePaths; // Stored image files for batched translations
    QString translatedMarkdown;
    QString prompt;
    // How many earlier translations of the conversation were sent as context for this
    // entry. 0 = translated standalone. Persisted so the marker survives a restart.
    int contextTurns = 0;
    QStringList tags;  // User-defined tags for categorization
    
    // UI-related fields
    QPoint lastPosition;
    QSize lastSize;
    bool hasLastPosition = false;
};

#endif // TRANSLATIONENTRY_H
