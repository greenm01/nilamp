import("stdfaust.lib");
hk = library("hk_filters.lib");

process = hk.flt_sv2_peq(2.0, 1000, 1.0, 1, 1);
