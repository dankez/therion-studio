#include "TherionScrap3D.h"
#include "TherionTokenRules.h"

#include <cmath>

namespace TherionStudio
{

QPointF TherionScrapTransform::transformPoint(const QPointF &scrapPoint) const
{
    const double cosTheta = std::cos(rotation);
    const double sinTheta = std::sin(rotation);
    const double x = scrapPoint.x();
    const double y = scrapPoint.y();
    const double tx_out = scale * (cosTheta * x - sinTheta * y) + tx;
    const double ty_out = scale * (sinTheta * x + cosTheta * y) + ty;
    return QPointF(tx_out, ty_out);
}

double TherionScrapTransform::interpolateZ(const QPointF &scrapPoint,
                                          const QVector<TherionScrapAnchor> &matchedAnchors) const
{
    if (matchedAnchors.isEmpty()) {
        return 0.0;
    }
    if (matchedAnchors.size() == 1) {
        return matchedAnchors.first().centrelinePosition.z;
    }

    double sumWeights = 0.0;
    double sumZ = 0.0;
    for (const auto &anchor : matchedAnchors) {
        const double dx = scrapPoint.x() - anchor.scrapPoint.x();
        const double dy = scrapPoint.y() - anchor.scrapPoint.y();
        const double distSq = dx * dx + dy * dy;
        if (distSq < 1e-4) {
            return anchor.centrelinePosition.z;
        }
        const double weight = 1.0 / distSq;
        sumWeights += weight;
        sumZ += weight * anchor.centrelinePosition.z;
    }

    return sumWeights > 0.0 ? (sumZ / sumWeights) : matchedAnchors.first().centrelinePosition.z;
}

TherionScrapScale TherionScrap3D::parseScaleOption(const QStringList &tokens)
{
    TherionScrapScale result;
    if (tokens.isEmpty()) {
        return result;
    }

    // Clean tokens: remove brackets e.g. "[-128" -> "-128", "m]" -> "m"
    QStringList cleaned;
    for (const QString &rawToken : tokens) {
        QString token = rawToken.trimmed();
        while (token.startsWith(QLatin1Char('['))) {
            token.remove(0, 1);
        }
        while (token.endsWith(QLatin1Char(']'))) {
            token.chop(1);
        }
        token = token.trimmed();
        if (!token.isEmpty()) {
            cleaned.append(token);
        }
    }

    // Therion scrap scale format: [x1 y1 x2 y2 X1 Y1 X2 Y2 <unit>]
    // Minimum 8 numeric tokens:
    if (cleaned.size() < 8) {
        return result;
    }

    bool ok = true;
    const double x1 = cleaned.at(0).toDouble(&ok);
    if (!ok) return result;
    const double y1 = cleaned.at(1).toDouble(&ok);
    if (!ok) return result;
    const double x2 = cleaned.at(2).toDouble(&ok);
    if (!ok) return result;
    const double y2 = cleaned.at(3).toDouble(&ok);
    if (!ok) return result;
    const double X1 = cleaned.at(4).toDouble(&ok);
    if (!ok) return result;
    const double Y1 = cleaned.at(5).toDouble(&ok);
    if (!ok) return result;
    const double X2 = cleaned.at(6).toDouble(&ok);
    if (!ok) return result;
    const double Y2 = cleaned.at(7).toDouble(&ok);
    if (!ok) return result;

    double unitFactor = 1.0;
    if (cleaned.size() >= 9) {
        const QString unit = cleaned.at(8).toLower();
        if (unit == QLatin1String("ft") || unit == QLatin1String("feet")) {
            unitFactor = 0.3048;
        } else if (unit == QLatin1String("cm")) {
            unitFactor = 0.01;
        } else if (unit == QLatin1String("in") || unit == QLatin1String("inch")) {
            unitFactor = 0.0254;
        } else if (unit == QLatin1String("yd") || unit == QLatin1String("yard")) {
            unitFactor = 0.9144;
        }
    }

    const double scrapDist = std::hypot(x2 - x1, y2 - y1);
    const double worldDist = std::hypot(X2 - X1, Y2 - Y1) * unitFactor;

    if (scrapDist > 1e-4) {
        result.p1 = QPointF(x1, y1);
        result.p2 = QPointF(x2, y2);
        result.worldLengthMeters = worldDist;
        result.metersPerPoint = worldDist / scrapDist;
        result.valid = true;
    }

    return result;
}

QString TherionScrap3D::resolveStationKey(
    const QString &stationName,
    const QString &scrapSurveyPath,
    const QHash<QString, ThreeDViewerVec3> &stationPositions)
{
    if (stationName.isEmpty()) {
        return QString();
    }

    // 1. Direct exact match
    if (stationPositions.contains(stationName)) {
        return stationName;
    }

    // 2. Qualified with scrap survey path if available (name@path or name.path)
    if (!scrapSurveyPath.isEmpty()) {
        const QString qualified = stationName.contains(QLatin1Char('@'))
            ? QStringLiteral("%1.%2").arg(stationName, scrapSurveyPath)
            : QStringLiteral("%1@%2").arg(stationName, scrapSurveyPath);
        if (stationPositions.contains(qualified)) {
            return qualified;
        }
    }

    // 3. Match by suffix or prefix (e.g. "0@hp" matches "0@hp.1302")
    const QString nameAt = stationName.contains(QLatin1Char('@')) ? stationName : (stationName + QLatin1Char('@'));
    for (auto it = stationPositions.cbegin(); it != stationPositions.cend(); ++it) {
        const QString &key = it.key();
        if (key.startsWith(nameAt + QLatin1Char('.')) || key == stationName) {
            return key;
        }
    }

    // 4. Try matching station name ignoring survey prefix/suffix
    const QString baseName = stationName.section(QLatin1Char('@'), 0, 0);
    for (auto it = stationPositions.cbegin(); it != stationPositions.cend(); ++it) {
        const QString &key = it.key();
        if (key.section(QLatin1Char('@'), 0, 0) == baseName) {
            return key;
        }
    }

    return QString();
}

TherionScrapTransform TherionScrap3D::fitTransform(
    const QVector<TherionScrapAnchor> &anchors,
    const TherionScrapScale &scaleOption)
{
    TherionScrapTransform transform;
    transform.anchorCount = anchors.size();

    QVector<TherionScrapAnchor> matched;
    for (const auto &anchor : anchors) {
        if (anchor.matched) {
            matched.append(anchor);
        }
    }
    transform.matchedAnchorCount = matched.size();

    if (matched.isEmpty()) {
        if (scaleOption.valid) {
            transform.scale = scaleOption.metersPerPoint;
            transform.valid = true;
        }
        return transform;
    }

    if (matched.size() == 1) {
        const auto &anchor = matched.first();
        transform.scale = scaleOption.valid ? scaleOption.metersPerPoint : 0.0254;
        transform.rotation = 0.0;
        transform.tx = anchor.centrelinePosition.x - transform.scale * anchor.scrapPoint.x();
        transform.ty = anchor.centrelinePosition.y - transform.scale * anchor.scrapPoint.y();
        transform.valid = true;
        return transform;
    }

    // Procrustes 2D similarity fit for N >= 2 anchors
    const int N = matched.size();
    double mean_px = 0.0;
    double mean_py = 0.0;
    double mean_Px = 0.0;
    double mean_Py = 0.0;

    for (const auto &a : matched) {
        mean_px += a.scrapPoint.x();
        mean_py += a.scrapPoint.y();
        mean_Px += a.centrelinePosition.x;
        mean_Py += a.centrelinePosition.y;
    }
    mean_px /= N;
    mean_py /= N;
    mean_Px /= N;
    mean_Py /= N;

    double var_p = 0.0;
    double Sxx = 0.0;
    double Syy = 0.0;
    double Sxy = 0.0;
    double Syx = 0.0;

    for (const auto &a : matched) {
        const double ux = a.scrapPoint.x() - mean_px;
        const double uy = a.scrapPoint.y() - mean_py;
        const double Ux = a.centrelinePosition.x - mean_Px;
        const double Uy = a.centrelinePosition.y - mean_Py;

        var_p += (ux * ux + uy * uy);
        Sxx += ux * Ux;
        Syy += uy * Uy;
        Sxy += ux * Uy;
        Syx += uy * Ux;
    }

    if (var_p < 1e-6) {
        // Degenerate points: fallback to 1 anchor translation
        const auto &anchor = matched.first();
        transform.scale = scaleOption.valid ? scaleOption.metersPerPoint : 0.0254;
        transform.rotation = 0.0;
        transform.tx = anchor.centrelinePosition.x - transform.scale * anchor.scrapPoint.x();
        transform.ty = anchor.centrelinePosition.y - transform.scale * anchor.scrapPoint.y();
        transform.valid = true;
        return transform;
    }

    const double a = Sxx + Syy;
    const double b = Sxy - Syx;
    const double theta = std::atan2(b, a);
    const double s = std::hypot(a, b) / var_p;

    const double cosTheta = std::cos(theta);
    const double sinTheta = std::sin(theta);

    transform.scale = s;
    transform.rotation = theta;
    transform.tx = mean_Px - s * (cosTheta * mean_px - sinTheta * mean_py);
    transform.ty = mean_Py - s * (sinTheta * mean_px + cosTheta * mean_py);
    transform.valid = true;

    // Calculate residuals
    double sumResidual = 0.0;
    double maxResidual = 0.0;
    for (const auto &anchor : matched) {
        const QPointF fitted = transform.transformPoint(anchor.scrapPoint);
        const double dx = fitted.x() - anchor.centrelinePosition.x;
        const double dy = fitted.y() - anchor.centrelinePosition.y;
        const double res = std::hypot(dx, dy);
        sumResidual += res;
        maxResidual = std::max(maxResidual, res);
    }

    transform.meanResidual = sumResidual / N;
    transform.maxResidual = maxResidual;

    return transform;
}

TherionScrap3DModel TherionScrap3D::projectScrap(
    const Th2GeometryProjection &th2Projection,
    const QString &scrapId,
    const QString &scrapSurveyPath,
    const QHash<QString, ThreeDViewerVec3> &stationPositions,
    quint32 meshId,
    quint32 surveyId)
{
    TherionScrap3DModel model;
    model.scrapId = scrapId;

    // Find scrap block options
    TherionScrapScale scaleOption;
    for (const auto &block : th2Projection.blockObjects()) {
        const QString blockScrapId = !block.type.isEmpty() ? block.type : block.id;
        if (block.kind == Th2GeometryObjectKind::Scrap && (scrapId.isEmpty() || blockScrapId == scrapId)) {
            model.scrapId = blockScrapId;
            model.projection = block.optionsByName.value(QStringLiteral("projection")).join(QLatin1Char(' '));
            if (model.projection.isEmpty()) {
                model.projection = block.optionsByName.value(QStringLiteral("-projection")).join(QLatin1Char(' '));
            }
            QStringList scaleTokens = block.optionsByName.value(QStringLiteral("scale"));
            if (scaleTokens.isEmpty()) {
                scaleTokens = block.optionsByName.value(QStringLiteral("-scale"));
            }
            scaleOption = parseScaleOption(scaleTokens);
            break;
        }
    }

    // Extract station anchors
    for (const auto &point : th2Projection.points()) {
        if (point.command.type.compare(QStringLiteral("station"), Qt::CaseInsensitive) == 0 && point.hasPosition) {
            QString stationName = point.command.id;
            if (stationName.isEmpty()) {
                const QStringList nameTokens = point.command.optionsByName.value(QStringLiteral("name"));
                if (!nameTokens.isEmpty()) {
                    stationName = nameTokens.first();
                } else {
                    const QStringList dashTokens = point.command.optionsByName.value(QStringLiteral("-name"));
                    if (!dashTokens.isEmpty()) {
                        stationName = dashTokens.first();
                    }
                }
            }
            if (!stationName.isEmpty()) {
                TherionScrapAnchor anchor;
                anchor.stationName = stationName;
                anchor.scrapPoint = point.position;
                anchor.resolvedStationKey = resolveStationKey(anchor.stationName, scrapSurveyPath, stationPositions);
                if (!anchor.resolvedStationKey.isEmpty() && stationPositions.contains(anchor.resolvedStationKey)) {
                    anchor.centrelinePosition = stationPositions.value(anchor.resolvedStationKey);
                    anchor.matched = true;
                }
                model.anchors.append(anchor);
            }
        }
    }

    model.transform = fitTransform(model.anchors, scaleOption);

    // Filter matched anchors for Z-interpolation
    QVector<TherionScrapAnchor> matchedAnchors;
    for (const auto &a : model.anchors) {
        if (a.matched) {
            matchedAnchors.append(a);
        }
    }

    // Compute residuals for each anchor
    for (auto &anchor : model.anchors) {
        if (anchor.matched) {
            const QPointF fitted = model.transform.transformPoint(anchor.scrapPoint);
            anchor.residual = std::hypot(fitted.x() - anchor.centrelinePosition.x,
                                         fitted.y() - anchor.centrelinePosition.y);
        }
    }

    if (!model.transform.valid) {
        return model;
    }

    // Extract lines (especially "wall" lines) and build 3D mesh
    model.meshGroup.id = meshId;
    model.meshGroup.surveyId = surveyId;

    const double wallHalfHeight = 1.0; // Standard 2m vertical ribbon

    for (const auto &line : th2Projection.lines()) {
        if (!line.command.type.startsWith(QLatin1String("wall"), Qt::CaseInsensitive)) {
            continue;
        }

        // Reconstruct polyline points with Bezier subdivision
        QVector<QPointF> polyline2D;
        for (const auto &row : line.pointRows) {
            if (row.coordinatePoints.isEmpty()) {
                continue;
            }

            if (polyline2D.isEmpty()) {
                polyline2D.append(row.coordinatePoints.first());
                continue;
            }

            if (row.coordinatePoints.size() == 1) {
                polyline2D.append(row.coordinatePoints.first());
            } else if (row.coordinatePoints.size() >= 3) {
                // Cubic Bezier curve
                const QPointF p0 = polyline2D.last();
                const QPointF p1 = row.coordinatePoints.at(0);
                const QPointF p2 = row.coordinatePoints.at(1);
                const QPointF p3 = row.coordinatePoints.at(2);

                constexpr int kSteps = 4;
                for (int step = 1; step <= kSteps; ++step) {
                    const double t = double(step) / double(kSteps);
                    const double u = 1.0 - t;
                    const double tt = t * t;
                    const double uu = u * u;
                    const double uuu = uu * u;
                    const double ttt = tt * t;

                    const double bx = uuu * p0.x() + 3.0 * uu * t * p1.x() + 3.0 * u * tt * p2.x() + ttt * p3.x();
                    const double by = uuu * p0.y() + 3.0 * uu * t * p1.y() + 3.0 * u * tt * p2.y() + ttt * p3.y();
                    polyline2D.append(QPointF(bx, by));
                }
            }
        }

        if (polyline2D.size() < 2) {
            continue;
        }

        // Convert 2D polyline to 3D points
        QVector<ThreeDViewerVec3> polyline3D;
        polyline3D.reserve(polyline2D.size());
        for (const QPointF &pt : polyline2D) {
            const QPointF horiz = model.transform.transformPoint(pt);
            const double z = model.transform.interpolateZ(pt, matchedAnchors);
            polyline3D.append(ThreeDViewerVec3{horiz.x(), horiz.y(), z});
        }
        model.wallPolylines.append(polyline3D);

        // Build extruded vertical ribbon triangles
        const quint32 baseVertexIndex = quint32(model.meshGroup.vertices.size());
        for (const auto &pt3d : polyline3D) {
            // Vertex 2k: floor
            model.meshGroup.vertices.append(ThreeDViewerVec3{pt3d.x, pt3d.y, pt3d.z - wallHalfHeight});
            // Vertex 2k+1: ceiling
            model.meshGroup.vertices.append(ThreeDViewerVec3{pt3d.x, pt3d.y, pt3d.z + wallHalfHeight});
        }

        const int numPoints = polyline3D.size();
        for (int k = 0; k < numPoints - 1; ++k) {
            const quint32 vFloor0 = baseVertexIndex + quint32(2 * k);
            const quint32 vCeil0  = baseVertexIndex + quint32(2 * k + 1);
            const quint32 vFloor1 = baseVertexIndex + quint32(2 * (k + 1));
            const quint32 vCeil1  = baseVertexIndex + quint32(2 * (k + 1) + 1);

            // Forward-facing quad
            model.meshGroup.triangles.push_back({vFloor0, vFloor1, vCeil0});
            model.meshGroup.triangles.push_back({vCeil0, vFloor1, vCeil1});
            // Back-facing quad
            model.meshGroup.triangles.push_back({vFloor0, vCeil0, vFloor1});
            model.meshGroup.triangles.push_back({vCeil0, vCeil1, vFloor1});
        }
    }

    model.meshGroup.computeNormals();
    return model;
}

QVector<TherionScrap3DModel> TherionScrap3D::projectTh2File(
    const QString &filePath,
    const QString &th2Contents,
    const QString &surveyPath,
    const QHash<QString, ThreeDViewerVec3> &stationPositions,
    quint32 startMeshId,
    quint32 surveyId)
{
    QVector<TherionScrap3DModel> results;
    const Th2GeometryProjection projection = Th2GeometryProjection::fromText(th2Contents);

    quint32 currentMeshId = startMeshId;
    for (const auto &block : projection.blockObjects()) {
        if (block.kind == Th2GeometryObjectKind::Scrap) {
            const QString scrapId = !block.type.isEmpty() ? block.type : block.id;
            TherionScrap3DModel model = projectScrap(
                projection, scrapId, surveyPath, stationPositions, currentMeshId++, surveyId);
            model.filePath = filePath;
            results.append(model);
        }
    }

    return results;
}

} // namespace TherionStudio
