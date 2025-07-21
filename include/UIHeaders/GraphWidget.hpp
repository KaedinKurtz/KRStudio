#pragma once

#include "GraphDataSource.hpp"
#include <vector>
#include <Eigen/Dense> // The core Eigen header for matrix and vector algebra.

// Holds the result of a simple 2D linear regression (y = mx + b).
struct LinearRegressionResult2D
{
    double slope = 0.0;       // The slope (m) of the best-fit line.
    double intercept = 0.0;   // The y-intercept (b).
    double rSquared = 0.0;    // R-squared, a measure of how well the line fits the data (0 to 1).
};

// Holds the result of an N-D linear regression (y = b + m1*x1 + m2*x2 + ...).
struct LinearRegressionResultND
{
    Eigen::VectorXd coefficients; // The coefficients [m1, m2, m3, ...].
    double intercept = 0.0;       // The intercept (b).
};

// A class dedicated to performing mathematical analysis on N-dimensional data.
class GraphAnalysisEngine
{
public:
    /**
     * @brief Performs a least-squares linear regression for a 2D projection of the data.
     * @param source The data provider.
     * @param independentDim The dimension to use as the independent variable ('x').
     * @param dependentDim The dimension to use as the dependent variable ('y').
     * @return A struct containing the slope, intercept, and R-squared value.
    */
    static LinearRegressionResult2D calculateLinearRegression2D(const GraphDataSource& source, size_t independentDim, size_t dependentDim);

    /**
     * @brief Performs N-dimensional linear regression to model one dimension as a function of others.
     * @param source The data provider.
     * @param independentDims The set of dimensions to use as independent variables ('x1', 'x2', ...).
     * @param dependentDim The dimension to model as the dependent variable ('y').
     * @return A struct containing the intercept and a vector of coefficients.
    */
    static LinearRegressionResultND calculateLinearRegressionND(const GraphDataSource& source, const std::vector<size_t>& independentDims, size_t dependentDim);
};