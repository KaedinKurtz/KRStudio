#pragma once

#include "GraphDataSource.hpp"
#include <vector>

// Forward declaration for a struct to hold regression results
struct LinearRegressionResult;

// A class dedicated to performing mathematical analysis on N-dimensional data.
class GraphAnalysisEngine
{
public:
    GraphAnalysisEngine() = default;

    /**
     * @brief Performs a least-squares linear regression on one dependent variable against one independent variable.
     * @param source The data to analyze.
     * @param independentDim The index of the dimension to use as the 'x' axis.
     * @param dependentDim The index of the dimension to use as the 'y' axis.
     * @return A struct containing the slope (m), intercept (b), and R-squared value.
    */
    static LinearRegressionResult calculateLinearRegression(const GraphDataSource* source, size_t independentDim, size_t dependentDim);

    /**
     * @brief Performs N-dimensional linear regression.
     * Models: y = b + m1*x1 + m2*x2 + ...
     * @param source The data to analyze.
     * @param independentDims A vector of dimension indices to use as the independent variables (x1, x2, ...).
     * @param dependentDim The single dimension index to use as the dependent variable (y).
     * @return A struct containing the coefficients (m1, m2, ...) and the intercept (b).
    */
    // Note: This is more advanced and would likely require a matrix library like Eigen.
    // static NDimensionalRegressionResult calculateNDRegression(const GraphDataSource* source, const std::vector<size_t>& independentDims, size_t dependentDim);


    /**
     * @brief Calculates statistical properties for a single dimension.
     * @param source The data to analyze.
     * @param dimension The index of the dimension to analyze.
     * @return A struct containing mean, median, standard deviation, etc.
    */
    // static StatisticsResult calculateStatistics(const GraphDataSource* source, size_t dimension);

    // ... other methods for clustering (e.g., K-Means), probability distributions, etc.
};