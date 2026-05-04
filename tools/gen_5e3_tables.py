import numpy as np
from gen_tables import gen_adnl_table, export_faust_table

def main():
    with open("dsp/5e3_tables.lib", "w") as f:
        f.write("// 5E3 ADNL Tables\n\n")
        
        # 12AY7 CC (T1)
        k0_12ay7 = 0.0012 / 0.0022
        t1_table = gen_adnl_table(k0_12ay7, 0, 0, 0)
        f.write(export_faust_table("t1_12ay7_table", t1_table))
        
        # 12AX7 CC (T1/T2)
        k0_12ax7 = 0.00076 / 0.00165
        t2_table = gen_adnl_table(k0_12ax7, 0, 0, 0)
        f.write(export_faust_table("t2_12ax7_table", t2_table))
        
        # 12AX7 CD (T3)
        k0_cd = 0.00073 / 0.00160
        # Cathodyne has kloop > 0
        # kloop = (rk + rl) * (1 + mu) / (ra + rl)
        # 5E3 cathodyne: ra=62500, rl=56000, rk=1500, mu=100 (Wait, 5E3 cathodyne has rl=rk=56k)
        # Keller's t3: mu=100, ra=62500, rl=56000, rk=1500? No, 5E3 usually has 56k/56k.
        # Keller's t3.tube_cd_set(100, 62500, 0.0016, 0.00073, 0, 0.5, 238, 56000, 1500, ...)
        # Wait, rk=1500 in Keller's code for CD? That's unusual for a cathodyne.
        # Ah, maybe he has a bias resistor separate from the load resistor.
        
        kloop_cd = (1500 + 56000) * (1 + 100) / (62500 + 56000)
        t3_table = gen_adnl_table(k0_cd, 0, 0, kloop_cd)
        f.write(export_faust_table("t3_cd_table", t3_table))
        
        # 6V6 Power Tube (T4/T5)
        # Keller's t4: mu=125, ra=40000, rl=3000, rk=540, isat=0.11, ibias=0.042, b=2
        k0_6v6 = 0.042 / 0.11
        kloop_6v6 = 540 * (1 + 125) / (40000 + 3000)
        t4_table = gen_adnl_table(k0_6v6, 2, 0, kloop_6v6)
        f.write(export_faust_table("t4_6v6_table", t4_table))

if __name__ == "__main__":
    main()
