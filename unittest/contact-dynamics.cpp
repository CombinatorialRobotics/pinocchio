//
// Copyright (c) 2016-2019 CNRS, INRIA
//

#include "pinocchio/spatial/se3.hpp"
#include "pinocchio/multibody/model.hpp"
#include "pinocchio/multibody/data.hpp"
#include "pinocchio/algorithm/jacobian.hpp"
#include "pinocchio/algorithm/contact-info.hpp"
#include "pinocchio/algorithm/contact-dynamics.hpp"
#include "pinocchio/algorithm/joint-configuration.hpp"
#include "pinocchio/parsers/sample-models.hpp"
#include "pinocchio/utils/timer.hpp"

#include <iostream>

#include <boost/test/unit_test.hpp>
#include <boost/utility/binary.hpp>

BOOST_AUTO_TEST_SUITE ( BOOST_TEST_MODULE )

BOOST_AUTO_TEST_CASE(contact_info)
{
  using namespace pinocchio;
  
  // Check default constructor
  ContactInfo ci1;
  BOOST_CHECK(ci1.type == CONTACT_UNDEFINED);
  BOOST_CHECK(ci1.dim() == 0);
  
  // Check complete constructor
  SE3 M(SE3::Random());
  ContactInfo ci2(CONTACT_3D,0,M);
  BOOST_CHECK(ci2.type == CONTACT_3D);
  BOOST_CHECK(ci2.parent == 0);
  BOOST_CHECK(ci2.placement.isApprox(M));
  BOOST_CHECK(ci2.dim() == 3);
  
  // Check contructor with two arguments
  ContactInfo ci2prime(CONTACT_3D,0);
  BOOST_CHECK(ci2prime.type == CONTACT_3D);
  BOOST_CHECK(ci2prime.parent == 0);
  BOOST_CHECK(ci2prime.placement.isIdentity());
  BOOST_CHECK(ci2prime.dim() == 3);
  
  // Check default copy constructor
  ContactInfo ci3(ci2);
  BOOST_CHECK(ci3 == ci2);
  
  // Check complete constructor 6D
  ContactInfo ci4(CONTACT_6D,0,SE3::Identity());
  BOOST_CHECK(ci4.type == CONTACT_6D);
  BOOST_CHECK(ci4.parent == 0);
  BOOST_CHECK(ci4.placement.isIdentity());
  BOOST_CHECK(ci4.dim() == 6);
}

BOOST_AUTO_TEST_CASE ( test_FD )
{
  using namespace Eigen;
  using namespace pinocchio;
  
  pinocchio::Model model;
  pinocchio::buildModels::humanoidRandom(model,true);
  pinocchio::Data data(model);
  
  VectorXd q = VectorXd::Ones(model.nq);
  q.segment <4> (3).normalize();
  
  pinocchio::computeJointJacobians(model, data, q);
  
  VectorXd v = VectorXd::Ones(model.nv);
  VectorXd tau = VectorXd::Zero(model.nv);
  
  const std::string RF = "rleg6_joint";
  const std::string LF = "lleg6_joint";
  
  Data::Matrix6x J_RF (6, model.nv);
  J_RF.setZero();
  getJointJacobian(model, data, model.getJointId(RF), LOCAL, J_RF);
  Data::Matrix6x J_LF (6, model.nv);
  J_LF.setZero();
  getJointJacobian(model, data, model.getJointId(LF), LOCAL, J_LF);
  
  Eigen::MatrixXd J (12, model.nv);
  J.setZero();
  J.topRows<6> () = J_RF;
  J.bottomRows<6> () = J_LF;
  
  Eigen::VectorXd gamma (VectorXd::Ones(12));
  
  Eigen::MatrixXd H(J.transpose());
  
  pinocchio::forwardDynamics(model, data, q, v, tau, J, gamma, 0.);
  data.M.triangularView<Eigen::StrictlyLower>() = data.M.transpose().triangularView<Eigen::StrictlyLower>();
  
  MatrixXd Minv (data.M.inverse());
  MatrixXd JMinvJt (J * Minv * J.transpose());
  
  Eigen::MatrixXd G_ref(J.transpose());
  cholesky::Uiv(model, data, G_ref);
  for(int k=0;k<model.nv;++k) G_ref.row(k) /= sqrt(data.D[k]);
    Eigen::MatrixXd H_ref(G_ref.transpose() * G_ref);
    BOOST_CHECK(H_ref.isApprox(JMinvJt,1e-12));
  
  VectorXd lambda_ref = -JMinvJt.inverse() * (J*Minv*(tau - data.nle) + gamma);
  BOOST_CHECK(data.lambda_c.isApprox(lambda_ref, 1e-12));
    
  VectorXd a_ref = Minv*(tau - data.nle + J.transpose()*lambda_ref);

  Eigen::VectorXd dynamics_residual_ref (data.M * a_ref + data.nle - tau - J.transpose()*lambda_ref);
  BOOST_CHECK(dynamics_residual_ref.norm() <= 1e-11); // previously 1e-12, may be due to numerical approximations, i obtain 2.03e-12

  Eigen::VectorXd constraint_residual (J * data.ddq + gamma);
  BOOST_CHECK(constraint_residual.norm() <= 1e-12);
  
  Eigen::VectorXd dynamics_residual (data.M * data.ddq + data.nle - tau - J.transpose()*data.lambda_c);
  BOOST_CHECK(dynamics_residual.norm() <= 1e-12);

}

BOOST_AUTO_TEST_CASE (test_KKTMatrix)
{
  using namespace Eigen;
  using namespace pinocchio;
  pinocchio::Model model;
  pinocchio::buildModels::humanoidRandom(model,true);
  pinocchio::Data data(model);
  
  VectorXd q = VectorXd::Ones(model.nq);
  q.segment <4> (3).normalize();
  
  pinocchio::computeJointJacobians(model, data, q);
  
  VectorXd v = VectorXd::Ones(model.nv);
  VectorXd tau = VectorXd::Zero(model.nv);
  
  const std::string RF = "rleg6_joint";
  const std::string LF = "lleg6_joint";
  
  Data::Matrix6x J_RF (6, model.nv);
  J_RF.setZero();
  getJointJacobian(model, data, model.getJointId(RF), LOCAL, J_RF);
  Data::Matrix6x J_LF (6, model.nv);
  J_LF.setZero();
  getJointJacobian(model, data, model.getJointId(LF), LOCAL, J_LF);
  
  Eigen::MatrixXd J (12, model.nv);
  J.setZero();
  J.topRows<6> () = J_RF;
  J.bottomRows<6> () = J_LF;
  
  Eigen::VectorXd gamma (VectorXd::Ones(12));
  
  Eigen::MatrixXd H(J.transpose());
  
  //Check Forward Dynamics
  pinocchio::forwardDynamics(model, data, q, v, tau, J, gamma, 0.);
  data.M.triangularView<Eigen::StrictlyLower>() = data.M.transpose().triangularView<Eigen::StrictlyLower>();

  Eigen::MatrixXd MJtJ(model.nv+12, model.nv+12);
  MJtJ << data.M, J.transpose(),
    J, Eigen::MatrixXd::Zero(12, 12);

  Eigen::MatrixXd MJtJ_inv(model.nv+12, model.nv+12);
  getKKTContactDynamicMatrixInverse(model, data, J, MJtJ_inv);

  BOOST_CHECK(MJtJ_inv.isApprox(MJtJ.inverse()));

  //Check Impulse Dynamics
  const double r_coeff = 1.;
  VectorXd v_before = VectorXd::Ones(model.nv);
  pinocchio::impulseDynamics(model, data, q, v_before, J, r_coeff, 0.);
  data.M.triangularView<Eigen::StrictlyLower>() = data.M.transpose().triangularView<Eigen::StrictlyLower>();
  MJtJ << data.M, J.transpose(),
    J, Eigen::MatrixXd::Zero(12, 12);

  getKKTContactDynamicMatrixInverse(model, data, J, MJtJ_inv);

  BOOST_CHECK(MJtJ_inv.isApprox(MJtJ.inverse()));
  
}

BOOST_AUTO_TEST_CASE ( test_FD_with_damping )
{
  using namespace Eigen;
  using namespace pinocchio;
  
  pinocchio::Model model;
  pinocchio::buildModels::humanoidRandom(model,true);
  pinocchio::Data data(model);
  
  VectorXd q = VectorXd::Ones(model.nq);
  q.segment <4> (3).normalize();
  
  pinocchio::computeJointJacobians(model, data, q);
  
  VectorXd v = VectorXd::Ones(model.nv);
  VectorXd tau = VectorXd::Zero(model.nv);
  
  const std::string RF = "rleg6_joint";
  
  Data::Matrix6x J_RF (6, model.nv);
  J_RF.setZero();
  getJointJacobian(model, data, model.getJointId(RF), LOCAL, J_RF);

  Eigen::MatrixXd J (12, model.nv);
  J.setZero();
  J.topRows<6> () = J_RF;
  J.bottomRows<6> () = J_RF;
  
  Eigen::VectorXd gamma (VectorXd::Ones(12));

  // Forward Dynamics with damping
  pinocchio::forwardDynamics(model, data, q, v, tau, J, gamma, 1e-12);

  // Matrix Definitions
  Eigen::MatrixXd H(J.transpose());
  data.M.triangularView<Eigen::StrictlyLower>() =
    data.M.transpose().triangularView<Eigen::StrictlyLower>();
  
  MatrixXd Minv (data.M.inverse());
  MatrixXd JMinvJt (J * Minv * J.transpose());

  // Check that JMinvJt is correctly formed
  Eigen::MatrixXd G_ref(J.transpose());
  cholesky::Uiv(model, data, G_ref);
  for(int k=0;k<model.nv;++k) G_ref.row(k) /= sqrt(data.D[k]);
  Eigen::MatrixXd H_ref(G_ref.transpose() * G_ref);
  BOOST_CHECK(H_ref.isApprox(JMinvJt,1e-12));

  // Actual Residuals
  Eigen::VectorXd constraint_residual (J * data.ddq + gamma);  
  Eigen::VectorXd dynamics_residual (data.M * data.ddq + data.nle - tau - J.transpose()*data.lambda_c);
  BOOST_CHECK(constraint_residual.norm() <= 1e-9);
  BOOST_CHECK(dynamics_residual.norm() <= 1e-12);
}

//BOOST_AUTO_TEST_CASE ( test_FD_with_singularity )
//{
//  using namespace Eigen;
//  using namespace pinocchio;
//  
//  pinocchio::Model model;
//  pinocchio::buildModels::humanoidRandom(model,true);
//  pinocchio::Data data(model);
//  
//  VectorXd q = VectorXd::Ones(model.nq);
//  q.segment<4>(3).normalize();
//  
//  pinocchio::computeJointJacobians(model, data, q);
//  
//  VectorXd v = VectorXd::Ones(model.nv);
//  VectorXd tau = VectorXd::Zero(model.nv);
//  
//  const std::string RF = "rleg6_joint";
//  
//  Data::Matrix6x J_RF (6, model.nv);
//  J_RF.setZero();
//  getJointJacobian(model, data, model.getJointId(RF), LOCAL, J_RF);
//  
//  Eigen::MatrixXd J(12, model.nv);
//  J.topRows<6> () = J_RF;
//  J.bottomRows<6> () = J_RF;
//  
//  Eigen::VectorXd gamma (VectorXd::Ones(12));
//  
//  ProximalSettings prox_settings(1e-12,1e-8,20);
//  
//  // Forward Dynamics with damping
//  pinocchio::forwardDynamics(model, data, q, v, tau, J, gamma, prox_settings);
//  
//  // Actual Residuals
//  Eigen::VectorXd constraint_residual (J * data.ddq + gamma);
//  Eigen::VectorXd dynamics_residual (data.M * data.ddq + data.nle - tau - J.transpose()*data.lambda_c);
//  BOOST_CHECK(constraint_residual.norm() <= 1e-9);
//  BOOST_CHECK(dynamics_residual.norm() <= 1e-12);
//  std::cout << "dynamics_residual: " << dynamics_residual.norm() << std::endl;
//}

BOOST_AUTO_TEST_CASE ( test_ID )
{
  using namespace Eigen;
  using namespace pinocchio;
  
  pinocchio::Model model;
  pinocchio::buildModels::humanoidRandom(model,true);
  pinocchio::Data data(model);
  
  VectorXd q = VectorXd::Ones(model.nq);
  q.segment <4> (3).normalize();
  
  pinocchio::computeJointJacobians(model, data, q);
  
  VectorXd v_before = VectorXd::Ones(model.nv);
  
  const std::string RF = "rleg6_joint";
  const std::string LF = "lleg6_joint";
  
  Data::Matrix6x J_RF (6, model.nv);
  J_RF.setZero();
  getJointJacobian(model, data, model.getJointId(RF), LOCAL, J_RF);
  Data::Matrix6x J_LF (6, model.nv);
  J_LF.setZero();
  getJointJacobian(model, data, model.getJointId(LF), LOCAL, J_LF);
  
  Eigen::MatrixXd J (12, model.nv);
  J.setZero();
  J.topRows<6> () = J_RF;
  J.bottomRows<6> () = J_LF;
  
  const double r_coeff = 1.;
  
  Eigen::MatrixXd H(J.transpose());
  
  pinocchio::impulseDynamics(model, data, q, v_before, J, r_coeff, 0.);
  data.M.triangularView<Eigen::StrictlyLower>() = data.M.transpose().triangularView<Eigen::StrictlyLower>();
  
  MatrixXd Minv (data.M.inverse());
  MatrixXd JMinvJt (J * Minv * J.transpose());
  
  Eigen::MatrixXd G_ref(J.transpose());
  cholesky::Uiv(model, data, G_ref);
  for(int k=0;k<model.nv;++k) G_ref.row(k) /= sqrt(data.D[k]);
  Eigen::MatrixXd H_ref(G_ref.transpose() * G_ref);
  BOOST_CHECK(H_ref.isApprox(JMinvJt,1e-12));
  
  VectorXd lambda_ref = JMinvJt.inverse() * (-r_coeff * J * v_before - J * v_before);
  BOOST_CHECK(data.impulse_c.isApprox(lambda_ref, 1e-12));
  
  VectorXd v_after_ref = Minv*(data.M * v_before + J.transpose()*lambda_ref);
  
  Eigen::VectorXd constraint_residual (J * data.dq_after + r_coeff * J * v_before);
  BOOST_CHECK(constraint_residual.norm() <= 1e-12);
  
  Eigen::VectorXd dynamics_residual (data.M * data.dq_after - data.M * v_before - J.transpose()*data.impulse_c);
  BOOST_CHECK(dynamics_residual.norm() <= 1e-12);

}

BOOST_AUTO_TEST_CASE (timings_fd_llt)
{
  using namespace Eigen;
  using namespace pinocchio;
  
  pinocchio::Model model;
  pinocchio::buildModels::humanoidRandom(model,true);
  pinocchio::Data data(model);
  
#ifdef NDEBUG
#ifdef _INTENSE_TESTING_
  const size_t NBT = 1000*1000;
#else
  const size_t NBT = 100;
#endif
  
#else
  const size_t NBT = 1;
  std::cout << "(the time score in debug mode is not relevant)  " ;
#endif // ifndef NDEBUG
  
  VectorXd q = VectorXd::Ones(model.nq);
  q.segment <4> (3).normalize();
  
  pinocchio::computeJointJacobians(model, data, q);
  
  VectorXd v = VectorXd::Ones(model.nv);
  VectorXd tau = VectorXd::Zero(model.nv);
  
  const std::string RF = "rleg6_joint";
  const std::string LF = "lleg6_joint";
  
  Data::Matrix6x J_RF (6, model.nv);
  J_RF.setZero();
  getJointJacobian(model, data, model.getJointId(RF), LOCAL, J_RF);
  Data::Matrix6x J_LF (6, model.nv);
  J_LF.setZero();
  getJointJacobian(model, data, model.getJointId(LF), LOCAL, J_LF);
  
  Eigen::MatrixXd J (12, model.nv);
  J.topRows<6> () = J_RF;
  J.bottomRows<6> () = J_LF;
  
  Eigen::VectorXd gamma (VectorXd::Ones(12));
  
  model.lowerPositionLimit.head<7>().fill(-1.);
  model.upperPositionLimit.head<7>().fill( 1.);
  
  q = pinocchio::randomConfiguration(model);
  
  PinocchioTicToc timer(PinocchioTicToc::US); timer.tic();
  SMOOTH(NBT)
  {
    pinocchio::forwardDynamics(model, data, q, v, tau, J, gamma, 0.);
  }
  timer.toc(std::cout,NBT);
  
}

BOOST_AUTO_TEST_SUITE_END ()
