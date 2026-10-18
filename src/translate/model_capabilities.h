#ifndef SNAPLENS_MODEL_CAPABILITIES_H
#define SNAPLENS_MODEL_CAPABILITIES_H

#include <QJsonObject>
#include <QStringList>
#include <optional>

// Endpoint/model-specific declarations, not a provider-wide capability guess.
// nullopt means unknown; an empty declared list is different from no declaration.
struct ModelCapabilities {
    enum class Source { Unknown, Upstream, Documentation };
    Source effortSource = Source::Unknown;
    Source thinkingSource = Source::Unknown;
    std::optional<bool> reasoning;
    std::optional<QStringList> efforts;
    std::optional<QStringList> thinkingModes;
    std::optional<QStringList> parameters;
    QString defaultEffort;
    QString thinkingFormat;
    QString outputField;
    qint64 contextWindow = 0;
    qint64 maxInputTokens = 0;
    qint64 maxOutputTokens = 0;

    /// Display-only fallback; never used to inject parameters into a request.
    static ModelCapabilities suggestions(const QJsonObject& metadata, const QString& apiBase,
                                         const QString& model);
    static ModelCapabilities fromMetadata(const QJsonObject& model);
    // Documented suggestions only for an exact first-party endpoint/model match.
    // Never merge these into the fetched metadata or treat them as API declarations.
    static std::optional<ModelCapabilities> documentedFallback(const QString& apiBase,
                                                               const QString& model);
    static QJsonObject cachedModel(const QJsonObject& cache, const QString& apiBase,
                                   const QString& model);
    QStringList effortChoices() const;
};

#endif
