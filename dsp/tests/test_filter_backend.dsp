// SPDX-License-Identifier: MIT
// Backend filter regression harness for JSFX mode-0 power-chain filters.
import("stdfaust.lib");
flt = library("hk_filters.lib");

k1_mode0 = 0.797;
k2_mode0 = 0.940;
hp3_hz = 5.8;
hp4_hz = 6.4;
kp1 = 1.1220184543;
fp_hz = 80.0;
qp1 = 2.6685237666;
ks1 = 1.4125375446;
fs1_hz = 2098.1359672;

hp3(x) = flt.flt_ii1_hp(hp3_hz, x);
hp4(x) = flt.flt_ii1_hp(hp4_hz, x);
peq1(x) = flt.flt_sv2_peq(kp1, fp_hz, qp1, 1, 1, x);
hs1(x) = flt.flt_sv1_hs(ks1, fs1_hz, 1, x);
peq1_hs1(x) = x : peq1 : hs1;
t4_pre_chain(x) = x : *(k1_mode0) : hp3 : peq1 : hs1;
t5_pre_chain(x) = x : *(k2_mode0) : hp4 : peq1 : hs1;

process(x) = hp3(x), hp4(x), peq1(x), hs1(x), peq1_hs1(x), t4_pre_chain(x), t5_pre_chain(x);
