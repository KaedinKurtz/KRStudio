#pragma once
// ConnectControlGate.hpp -- Phase G GATE CONNECT-AND-CONTROL declaration.
namespace krs::nodes {

// GATE CONNECT-AND-CONTROL (KRS_CONNECTCTRL_SELFTEST): a robot-control program built by wiring nodes, with
// a value set THROUGH a rendered input widget, driven by the live time source, drives the live robot;
// every stage asserted, severing a wire localizes the break. Needs QApplication.
bool runConnectControlGate();

} // namespace krs::nodes
