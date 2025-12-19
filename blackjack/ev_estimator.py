# Create a picklable EV estimator with built-in normalization
import pickle
from sklearn.base import BaseEstimator, TransformerMixin, RegressorMixin
import numpy as np
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import PolynomialFeatures
from sklearn.linear_model import LinearRegression


class CardProportionTransformer(BaseEstimator, TransformerMixin):
    """
    Transforms raw card counts to proportions.
    Input: array of shape (n_samples, 10) with columns [n2, n3, n4, n5, n6, n7, n8, n9, n10, nA]
    Output: array of shape (n_samples, 10) with proportions (each row sums to 1)
    """
    def __init__(self, keep_total_count=False, keep_individual_counts=False):
        self.keep_total_count = keep_total_count
        self.keep_individual_counts = keep_individual_counts
        self.feature_names = [f"prop_{r}" for r in range(2, 11)] + ["prop_A"]
        if keep_total_count:
            self.feature_names += ["total_count"]
        if keep_individual_counts:
            self.feature_names += [f"n{r}" for r in range(2, 11)] + ["nA"]
    
    def fit(self, X, y=None):
        return self
    
    def transform(self, X):
        X = np.asarray(X)
        totals = X.sum(axis=1, keepdims=True)
        # Avoid division by zero
        totals = np.where(totals == 0, 1, totals)

        X_out = X / totals
        if self.keep_total_count:
            X_out = np.column_stack([X_out, totals])
        if self.keep_individual_counts:
            X_out = np.column_stack([X_out, X])
        
        return X_out
    
    def get_feature_names_out(self, input_features=None):
        return self.feature_names


class EVEstimator(BaseEstimator, RegressorMixin):
    """
    Complete EV estimator that takes raw card counts and predicts EV.
    Includes all normalization and polynomial transformation.
    
    sklearn-compatible: can be used directly with cross_val_predict, GridSearchCV, etc.
    """
    def __init__(self, degree=3, keep_total_count=False, keep_individual_counts=False):
        self.degree = degree
        self.keep_total_count = keep_total_count
        self.keep_individual_counts = keep_individual_counts
        if keep_individual_counts and keep_total_count:
            raise ValueError("Cannot keep both total count and individual counts")
        self.pipeline = make_pipeline(
            CardProportionTransformer(keep_total_count=keep_total_count, keep_individual_counts=keep_individual_counts),
            PolynomialFeatures(degree=degree, include_bias=False),
            LinearRegression(fit_intercept=True)
        )
        self.is_fitted = False
        self.r2_train = None
        self.r2_cv = None
        
    def fit(self, X, y):
        """
        Fit the model.
        X: array of shape (n_samples, 10) - raw card counts [n2, n3, ..., n10, nA]
        y: array of shape (n_samples,) - EV values
        """
        self.pipeline.fit(X, y)
        self.is_fitted = True
        self.r2_train = self.pipeline.score(X, y)
        return self
    
    def predict(self, X):
        """
        Predict EV from raw card counts.
        X: array of shape (n_samples, 10) or (10,) - raw card counts
        """
        if isinstance(X, dict):
            X = np.array([X.get(r, 0) for r in range(2, 12)])
        X = np.atleast_2d(X)
        return self.pipeline.predict(X)
    
    def __repr__(self):
        status = "fitted" if self.is_fitted else "not fitted"
        r2_str = f", R²={self.r2_train:.4f}" if self.r2_train else ""
        return f"EVEstimator(degree={self.degree}, {status}{r2_str})"

