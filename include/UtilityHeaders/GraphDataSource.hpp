#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <string>
#include <utility> // For std::pair

// A pure virtual interface for any N-dimensional data provider.
class GraphDataSource : public QObject
{
    Q_OBJECT

public:
    explicit GraphDataSource(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~GraphDataSource() = default;

    // Returns the number of dimensions for each data point (e.g., 3 for XYZ, 6 for Pos+Vel).
    virtual size_t getDimensionCount() const = 0;

    // Returns the total number of data points available.
    virtual size_t getPointCount() const = 0;

    // Returns a human-readable name for a specific dimension (e.g., "Position X", "Joint Angle").
    virtual QString getDimensionName(size_t dimensionIndex) const = 0;

    // Returns the minimum and maximum values for a given dimension, for auto-scaling axes.
    virtual std::pair<double, double> getDimensionRange(size_t dimensionIndex) const = 0;

    // Fills a pre-allocated vector with the data for a single point.
    // This is efficient as it avoids repeated memory allocation.
    virtual void getPoint(size_t pointIndex, std::vector<double>& outPoint) const = 0;

signals:
    // Emitted when the underlying data has changed significantly and requires a full redraw/re-upload.
    void dataChanged();
};