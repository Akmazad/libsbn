// Copyright 2019-2020 libsbn project contributors.
// libsbn is free software under the GPLv3; see LICENSE file for details.
//
// A crappy version of Engine that only does JC, but does do our GP calculations.

#ifndef SRC_GP_ENGINE_HPP_
#define SRC_GP_ENGINE_HPP_

#include "eigen_sugar.hpp"
#include "gp_operation.hpp"
#include "site_pattern.hpp"
#include "substitution_model.hpp"

using NucleotidePLV = Eigen::Matrix<double, 4, Eigen::Dynamic, Eigen::RowMajor>;

class GPEngine {
 public:
  GPEngine(){};
  GPEngine(SitePattern site_pattern, size_t pcss_count);

  void operator()(const GPOperations::Zero& op);
  void operator()(const GPOperations::SetToStationaryDistribution& op);
  void operator()(const GPOperations::WeightedSumAccumulate& op);
  void operator()(const GPOperations::Multiply& op);
  void operator()(const GPOperations::Likelihood& op);
  void operator()(const GPOperations::EvolveRootward& op);
  void operator()(const GPOperations::EvolveLeafward& op);
  void operator()(const GPOperations::OptimizeRootward& op);
  void operator()(const GPOperations::OptimizeLeafward& op);
  void operator()(const GPOperations::UpdateSBNProbabilities& op);

  void ProcessOperations(GPOperationVector operations);

  void SetBranchLengthForTransitionMatrix(double branch_length);
  const Eigen::Matrix4d& GetTransitionMatrix() { return transition_matrix_; };
  void PrintPLV(size_t plv_idx);

  void SetBranchLengths(EigenVectorXd branch_lengths) {
    branch_lengths_ = branch_lengths;
  };
  double GetLogLikelihood(size_t plv_idx) { return log_likelihoods_(plv_idx); };

 private:
  SitePattern site_pattern_;
  size_t pcss_count_;
  std::vector<NucleotidePLV> plvs_;
  EigenVectorXd branch_lengths_;
  EigenVectorXd log_likelihoods_;
  EigenVectorXd q_;
  EigenVectorXd per_pattern_log_likelihoods_;

  JC69Model substitution_model_;
  Eigen::Matrix4d eigenmatrix_ = substitution_model_.GetEigenvectors().reshaped(4, 4);
  Eigen::Matrix4d inverse_eigenmatrix_ =
      substitution_model_.GetInverseEigenvectors().reshaped(4, 4);
  Eigen::Vector4d eigenvalues_ = substitution_model_.GetEigenvalues();
  Eigen::DiagonalMatrix<double, 4> diagonal_matrix_;
  Eigen::Matrix4d transition_matrix_;
  Eigen::Vector4d stationary_distribution_ = substitution_model_.GetFrequencies();
  EigenVectorXd site_pattern_weights_;

  void InitializePLVsWithSitePatterns();
};

#ifdef DOCTEST_LIBRARY_INCLUDED

TEST_CASE("GPEngine") {
  GPEngine engine;

  engine.SetBranchLengthForTransitionMatrix(0.75);
  // Computed directly:
  // https://en.wikipedia.org/wiki/Models_of_DNA_evolution#JC69_model_%28Jukes_and_Cantor_1969%29
  CHECK(fabs(0.52590958087 - engine.GetTransitionMatrix()(0, 0)) < 1e-10);
  CHECK(fabs(0.1580301397 - engine.GetTransitionMatrix()(0, 1)) < 1e-10);
}

#endif  // DOCTEST_LIBRARY_INCLUDED

#endif  // SRC_GP_ENGINE_HPP_
